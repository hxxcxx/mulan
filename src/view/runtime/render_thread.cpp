/**
 * @file render_thread.cpp
 * @brief RenderThread 的多 Surface 公平调度与通道状态实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "runtime/detail/render_thread.h"

#include "runtime/detail/render_executor.h"
#include "runtime/detail/render_device_context.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/rhi/engine_error_code.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>
#include <exception>
#include <future>
#include <utility>

namespace mulan::view::detail {
namespace {

std::mutex registry_mutex;
std::vector<std::weak_ptr<RenderThread>> registry;
std::atomic<uint64_t> next_thread_id = 1;

bool compatibleConfig(const ViewConfig& lhs, const ViewConfig& rhs) {
    if (lhs.backend != rhs.backend || lhs.enableValidation != rhs.enableValidation || lhs.msaa != rhs.msaa ||
        lhs.bufferCount != rhs.bufferCount || lhs.vsync != rhs.vsync || lhs.depthBuffer != rhs.depthBuffer ||
        lhs.stencilBuffer != rhs.stencilBuffer) {
        return false;
    }
    return std::equal(std::begin(lhs.clearColor), std::end(lhs.clearColor), std::begin(rhs.clearColor));
}

}  // namespace

struct RenderThread::ControlTask {
    std::function<ResultVoid(RenderExecutor&)> execute;
    bool fatalOnFailure = false;
    bool initializesChannel = false;
    bool stopsChannel = false;
    uint64_t resourceBatchId = 0;
    std::function<void()> complete;
    std::function<void(const Error&)> fail;
};

struct RenderThread::Channel {
    enum class Lifecycle : uint8_t {
        Starting,
        Ready,
        Stopping,
        Stopped,
        Failed,
    };

    explicit Channel(RenderChannelId value) : id(value), executor(std::make_unique<RenderExecutor>()) {}

    RenderChannelId id = 0;
    std::unique_ptr<RenderExecutor> executor;
    std::deque<ControlTask> controls;
    std::optional<RenderSubmission> latestFrame;
    RenderChannelState state;
    RenderSurfaceState surfaceState;
    Lifecycle lifecycle = Lifecycle::Starting;
};

Error RenderThread::threadError(ErrorCode code, std::string_view message) {
    return Error::make(code, message);
}

Result<std::shared_ptr<RenderThread>> RenderThread::acquire(const ViewConfig& config) {
    std::scoped_lock registryLock(registry_mutex);
    if (config.backend != engine::GraphicsBackend::OpenGL) {
        for (auto it = registry.begin(); it != registry.end();) {
            if (auto thread = it->lock()) {
                if (thread->state() == RenderThreadState::Healthy && compatibleConfig(thread->config_, config)) {
                    return thread;
                }
                ++it;
            } else {
                it = registry.erase(it);
            }
        }
    }

    try {
        auto thread = std::shared_ptr<RenderThread>(new RenderThread(config));
        if (config.backend != engine::GraphicsBackend::OpenGL) {
            registry.emplace_back(thread);
        }
        return thread;
    } catch (const std::exception& error) {
        LOG_ERROR("[RenderThread] Thread creation failed: {}", error.what());
    } catch (...) {
        LOG_ERROR("[RenderThread] Thread creation failed with an unknown exception");
    }
    return std::unexpected(threadError(ErrorCode::Internal, "Failed to create render thread."));
}

RenderThread::RenderThread(const ViewConfig& config) : config_(config), thread_id_(next_thread_id.fetch_add(1)) {
    thread_ = std::jthread([this](std::stop_token token) { run(token); });
    {
        std::scoped_lock lock(mutex_);
        state_ = RenderThreadState::Healthy;
    }
    LOG_INFO("[RenderThread] Thread created: id={}, backend={}", thread_id_, static_cast<int>(config.backend));
}

RenderThread::~RenderThread() {
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
        state_ = RenderThreadState::Stopped;
        for (auto& [id, channel] : channels_) {
            channel->lifecycle = Channel::Lifecycle::Stopping;
            channel->latestFrame.reset();
            while (!channel->controls.empty()) {
                cancelled.push_back(std::move(channel->controls.front()));
                channel->controls.pop_front();
            }
        }
    }
    const Error cancellation = threadError(ErrorCode::InvalidArg, "Render request was cancelled during shutdown.");
    for (ControlTask& task : cancelled) {
        if (task.fail) {
            task.fail(cancellation);
        }
    }
    thread_.request_stop();
    wake_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
    LOG_INFO("[RenderThread] Thread destroyed: id={}", thread_id_);
}

Result<RenderChannelId> RenderThread::attachWindow(const ViewConfig& config, int width, int height) {
    return attach([this, config, width, height](RenderExecutor& executor) -> ResultVoid {
        auto context = ensureDeviceContext();
        if (!context)
            return std::unexpected(context.error());
        return executor.initWindow(std::move(*context), config, width, height);
    });
}

Result<std::shared_ptr<RenderDeviceContext>> RenderThread::ensureDeviceContext() {
    if (device_context_)
        return device_context_;
    auto created = RenderDeviceContext::create(config_);
    if (!created)
        return std::unexpected(created.error());
    device_context_ = std::move(*created);
    return device_context_;
}

Result<RenderChannelId> RenderThread::attach(Initializer initialize) {
    auto promise = std::make_shared<std::promise<ResultVoid>>();
    auto future = promise->get_future();
    RenderChannelId channelId = 0;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_ || state_ != RenderThreadState::Healthy) {
            return std::unexpected(threadError(ErrorCode::InvalidArg, "Render thread is stopping."));
        }
        channelId = next_channel_++;
        if (channelId == 0) {
            channelId = next_channel_++;
        }
        auto channel = std::make_shared<Channel>(channelId);
        channel->controls.push_back(ControlTask{
                .execute = std::move(initialize),
                .fatalOnFailure = true,
                .initializesChannel = true,
                .complete = [promise] { promise->set_value(ResultVoid{}); },
                .fail = [promise](const Error& error) { promise->set_value(std::unexpected(error)); },
        });
        channels_.emplace(channelId, std::move(channel));
        channel_order_.push_back(channelId);
        assertInvariantsLocked();
    }
    wake_.notify_one();

    try {
        ResultVoid initialized = future.get();
        if (!initialized) {
            detach(channelId);
            return std::unexpected(initialized.error());
        }
    } catch (const std::exception& error) {
        LOG_ERROR("[RenderThread] Channel initialization handshake failed: {}", error.what());
        detach(channelId);
        return std::unexpected(threadError(ErrorCode::Internal, "Render channel initialization handshake failed."));
    }
    return channelId;
}

void RenderThread::detach(RenderChannelId channelId) {
    if (channelId == 0) {
        return;
    }
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
        LOG_ERROR("[RenderThread] A channel cannot synchronously detach from its render thread");
        return;
    }

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        const auto known = channels_.find(channelId);
        if (known == channels_.end()) {
            return;
        }
        Channel& channel = *known->second;
        if (channel.lifecycle == Channel::Lifecycle::Stopped) {
            promise->set_value();
        } else {
            channel.lifecycle = Channel::Lifecycle::Stopping;
            channel.latestFrame.reset();
            cancelled = std::move(channel.controls);
            channel.controls.push_back(ControlTask{
                    .execute = [](RenderExecutor& executor) -> ResultVoid {
                        executor.shutdown();
                        return {};
                    },
                    .stopsChannel = true,
                    .complete = [promise] { promise->set_value(); },
                    .fail = [promise](const Error&) { promise->set_value(); },
            });
        }
    }
    const Error cancellation = threadError(ErrorCode::InvalidArg, "Render request was cancelled during shutdown.");
    for (ControlTask& task : cancelled) {
        if (task.fail) {
            task.fail(cancellation);
        }
    }
    wake_.notify_one();
    future.wait();

    {
        std::scoped_lock lock(mutex_);
        channels_.erase(channelId);
        std::erase(channel_order_, channelId);
        cursor_.clamp(channel_order_.size());
        assertInvariantsLocked();
    }
}

bool RenderThread::isReady(RenderChannelId channelId) const {
    std::scoped_lock lock(mutex_);
    const auto known = channels_.find(channelId);
    return state_ == RenderThreadState::Healthy && known != channels_.end() &&
           known->second->lifecycle == Channel::Lifecycle::Ready;
}

ResultVoid RenderThread::submitFrame(RenderChannelId channelId, RenderSubmission submission) {
    {
        std::scoped_lock lock(mutex_);
        const auto known = channels_.find(channelId);
        if (state_ != RenderThreadState::Healthy || known == channels_.end() ||
            known->second->lifecycle != Channel::Lifecycle::Ready) {
            ++rejected_work_count_;
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not ready.")));
        }
        Channel& channel = *known->second;
        auto enqueued = enqueueSubmissionResourcesLocked(channel, submission);
        if (!enqueued) {
            return std::unexpected(enqueued.error());
        }
        channel.latestFrame = std::move(submission);
    }
    wake_.notify_one();
    return {};
}

ResultVoid RenderThread::enqueueSubmissionResourcesLocked(Channel& channel, RenderSubmission& submission) {
    const bool hasPrepare = !submission.prepare.empty();
    const bool hasBatch = submission.resourceBatchId != 0;
    if (hasPrepare != hasBatch) {
        return std::unexpected(threadError(
                ErrorCode::InvalidArg,
                "Render submission resource batch id and prepare payload must either both exist or both be empty."));
    }
    if (hasPrepare) {
        if (channel.state.beginResourceBatch(submission.resourceBatchId)) {
            channel.latestFrame.reset();
            engine::RenderResourcePrepareList prepare = std::move(submission.prepare);
            channel.controls.push_back(ControlTask{
                    .execute = [prepare = std::move(prepare)](
                                       RenderExecutor& executor) { return executor.prepareResources(prepare); },
                    .fatalOnFailure = true,
                    .resourceBatchId = submission.resourceBatchId,
            });
        } else {
            submission.prepare.clear();
        }
    }
    submission.prepare.clear();
    return {};
}

Result<engine::RenderCaptureResult> RenderThread::capture(RenderChannelId channelId, RenderSubmission submission,
                                                          engine::RenderCaptureDesc desc) {
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
        return std::unexpected(threadError(ErrorCode::InvalidArg, "Capture cannot wait on the render thread."));
    }
    using CaptureResult = Result<engine::RenderCaptureResult>;
    auto promise = std::make_shared<std::promise<CaptureResult>>();
    auto outcome = std::make_shared<std::optional<CaptureResult>>();
    auto future = promise->get_future();
    {
        std::scoped_lock lock(mutex_);
        const auto known = channels_.find(channelId);
        if (state_ != RenderThreadState::Healthy || known == channels_.end() ||
            known->second->lifecycle != Channel::Lifecycle::Ready) {
            ++rejected_work_count_;
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not available.")));
        }
        Channel& channel = *known->second;
        auto enqueued = enqueueSubmissionResourcesLocked(channel, submission);
        if (!enqueued) {
            return std::unexpected(enqueued.error());
        }
        channel.controls.push_back(ControlTask{
                .execute = [submission = std::move(submission), desc,
                            outcome](RenderExecutor& executor) mutable -> ResultVoid {
                    *outcome = executor.capture(submission, desc);
                    if (!outcome->value()) {
                        return std::unexpected(outcome->value().error());
                    }
                    return {};
                },
                .complete = [promise, outcome] { promise->set_value(std::move(outcome->value())); },
                .fail = [promise](const Error& error) { promise->set_value(std::unexpected(error)); },
        });
    }
    wake_.notify_one();
    return future.get();
}

Result<RenderSurfaceState> RenderThread::resize(RenderChannelId channelId, int width, int height) {
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
        return std::unexpected(threadError(ErrorCode::InvalidArg, "Resize cannot wait on the render thread."));
    }
    if (width <= 0 || height <= 0) {
        return std::unexpected(threadError(ErrorCode::InvalidArg, "Resize dimensions must be positive."));
    }
    using ResizeResult = Result<RenderSurfaceState>;
    auto promise = std::make_shared<std::promise<ResizeResult>>();
    auto outcome = std::make_shared<std::optional<ResizeResult>>();
    auto future = promise->get_future();
    {
        std::scoped_lock lock(mutex_);
        const auto known = channels_.find(channelId);
        if (state_ != RenderThreadState::Healthy || known == channels_.end() ||
            known->second->lifecycle != Channel::Lifecycle::Ready) {
            ++rejected_work_count_;
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not available.")));
        }
        Channel& channel = *known->second;
        channel.latestFrame.reset();
        channel.controls.push_back(ControlTask{
                .execute = [width, height, outcome](RenderExecutor& executor) -> ResultVoid {
                    *outcome = executor.resize(width, height);
                    if (!outcome->value()) {
                        return std::unexpected(outcome->value().error());
                    }
                    return {};
                },
                .fatalOnFailure = true,
                .complete = [promise, outcome] { promise->set_value(std::move(outcome->value())); },
                .fail = [promise](const Error& error) { promise->set_value(std::unexpected(error)); },
        });
    }
    wake_.notify_one();
    return future.get();
}

void RenderThread::enableIBL(RenderChannelId channelId, std::string hdrPath) {
    if (!enqueue(channelId, ControlTask{
                                    .execute = [path = std::move(hdrPath)](RenderExecutor& executor) -> ResultVoid {
                                        executor.enableIBL(path);
                                        return {};
                                    },
                            })) {
        LOG_WARN("[RenderThread] IBL request ignored because the channel is not ready");
    }
}

ResultVoid RenderThread::clearAssetResources(RenderChannelId channelId) {
    {
        std::scoped_lock lock(mutex_);
        const auto known = channels_.find(channelId);
        if (state_ != RenderThreadState::Healthy || known == channels_.end() ||
            known->second->lifecycle != Channel::Lifecycle::Ready) {
            ++rejected_work_count_;
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not ready.")));
        }
        Channel& channel = *known->second;
        channel.latestFrame.reset();
        channel.state.invalidateResourceBatch();
        channel.controls.push_back(ControlTask{
                .execute = [](RenderExecutor& executor) -> ResultVoid { return executor.clearAssetResources(); },
                .fatalOnFailure = true,
        });
    }
    wake_.notify_one();
    return {};
}

bool RenderThread::enqueue(RenderChannelId channelId, ControlTask task) {
    {
        std::scoped_lock lock(mutex_);
        const auto known = channels_.find(channelId);
        if (state_ != RenderThreadState::Healthy || known == channels_.end() ||
            known->second->lifecycle != Channel::Lifecycle::Ready) {
            ++rejected_work_count_;
            return false;
        }
        known->second->controls.push_back(std::move(task));
    }
    wake_.notify_one();
    return true;
}

std::optional<uint64_t> RenderThread::takeCompletedResourceBatch(RenderChannelId channelId) {
    std::scoped_lock lock(mutex_);
    const auto known = channels_.find(channelId);
    return known == channels_.end() ? std::nullopt : known->second->state.takeCompletedResourceBatch();
}

std::optional<Error> RenderThread::failureSnapshot(RenderChannelId channelId) const {
    std::scoped_lock lock(mutex_);
    const auto known = channels_.find(channelId);
    return known == channels_.end() ? std::nullopt : known->second->state.failure();
}

RenderSurfaceState RenderThread::surfaceState(RenderChannelId channelId) const {
    std::scoped_lock lock(mutex_);
    const auto known = channels_.find(channelId);
    return known == channels_.end() ? RenderSurfaceState{} : known->second->surfaceState;
}

RenderThreadStats RenderThread::stats() const {
    std::scoped_lock lock(mutex_);
    size_t pendingControls = 0;
    size_t pendingFrames = 0;
    for (const auto& [id, channel] : channels_) {
        pendingControls += channel->controls.size();
        pendingFrames += channel->latestFrame.has_value() ? 1u : 0u;
    }
    return RenderThreadStats{
        .threadId = thread_id_,
        .channelCount = channels_.size(),
        .executedControlCount = executed_control_count_,
        .executedFrameCount = executed_frame_count_,
        .rejectedWorkCount = rejected_work_count_,
        .failureBroadcastCount = failure_broadcast_count_,
        .pendingControlCount = pendingControls,
        .pendingFrameCount = pendingFrames,
        .state = state_,
    };
}

RenderThreadState RenderThread::state() const {
    std::scoped_lock lock(mutex_);
    return state_;
}

void RenderThread::injectFailureForTesting(Error error) {
    failThread(error);
}

bool RenderThread::channelHasWorkLocked(const Channel& channel) const {
    if (!channel.controls.empty()) {
        return true;
    }
    return channel.lifecycle == Channel::Lifecycle::Ready && channel.latestFrame.has_value();
}

void RenderThread::assertInvariantsLocked() const {
#ifndef NDEBUG
    assert(channel_order_.size() == channels_.size());
    for (size_t index = 0; index < channel_order_.size(); ++index) {
        assert(channel_order_[index] != 0);
        assert(channels_.contains(channel_order_[index]));
        assert(std::find(channel_order_.begin() + static_cast<std::ptrdiff_t>(index + 1), channel_order_.end(),
                         channel_order_[index]) == channel_order_.end());
    }
    assert(state_ != RenderThreadState::Failed || thread_failure_.has_value());
#endif
}

bool RenderThread::hasWorkLocked() const {
    return std::any_of(channels_.begin(), channels_.end(),
                       [this](const auto& entry) { return channelHasWorkLocked(*entry.second); });
}

std::shared_ptr<RenderThread::Channel> RenderThread::selectReadyChannelLocked(bool& hasControl, ControlTask& control,
                                                                              std::optional<RenderSubmission>& frame) {
    hasControl = false;
    if (channel_order_.empty()) {
        return {};
    }
    const size_t count = channel_order_.size();
    const size_t start = cursor_.start(count);
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t index = (start + offset) % count;
        const auto known = channels_.find(channel_order_[index]);
        if (known == channels_.end() || !channelHasWorkLocked(*known->second)) {
            continue;
        }
        std::shared_ptr<Channel> channel = known->second;
        cursor_.selected(index, count);
        if (!channel->controls.empty()) {
            control = std::move(channel->controls.front());
            channel->controls.pop_front();
            hasControl = true;
        } else {
            frame = std::move(channel->latestFrame);
            channel->latestFrame.reset();
        }
        return channel;
    }
    return {};
}

void RenderThread::publishSurfaceStateLocked(Channel& channel) {
    channel.surfaceState = channel.executor->surfaceState();
}

void RenderThread::failChannel(const std::shared_ptr<Channel>& channel, const Error& error) {
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        channel->latestFrame.reset();
        channel->state.fail(error);
        if (channel->lifecycle == Channel::Lifecycle::Stopping) {
            return;
        }
        channel->lifecycle = Channel::Lifecycle::Failed;
        cancelled = std::move(channel->controls);
    }
    for (ControlTask& task : cancelled) {
        if (task.fail) {
            task.fail(error);
        }
    }
    wake_.notify_all();
}

void RenderThread::failThread(const Error& error) {
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        if (state_ == RenderThreadState::Failed || state_ == RenderThreadState::Stopped) {
            return;
        }
        state_ = RenderThreadState::Failed;
        thread_failure_ = error;
        ++failure_broadcast_count_;
        for (auto& [id, channel] : channels_) {
            channel->latestFrame.reset();
            channel->state.fail(error);
            if (channel->lifecycle == Channel::Lifecycle::Stopping) {
                continue;
            }
            channel->lifecycle = Channel::Lifecycle::Failed;
            while (!channel->controls.empty()) {
                cancelled.push_back(std::move(channel->controls.front()));
                channel->controls.pop_front();
            }
        }
        assertInvariantsLocked();
    }
    for (ControlTask& task : cancelled) {
        if (task.fail) {
            task.fail(error);
        }
    }
    LOG_CRITICAL("[RenderThread] Shared device failed: threadId={}, error={}", thread_id_, error.message);
    wake_.notify_all();
}

void RenderThread::run(std::stop_token stopToken) {
    MULAN_PROFILE_THREAD("RenderThread");

    while (!stopToken.stop_requested()) {
        bool hasControl = false;
        ControlTask control;
        std::optional<RenderSubmission> frame;
        std::shared_ptr<Channel> channel;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stopToken, [this] { return stopping_ || hasWorkLocked(); });
            if (stopToken.stop_requested() || stopping_) {
                break;
            }
            channel = selectReadyChannelLocked(hasControl, control, frame);
        }
        if (!channel) {
            continue;
        }

        if (!hasControl) {
            if (!frame) {
                continue;
            }
            try {
                auto rendered = channel->executor->executeFrame(*frame);
                if (!rendered) {
                    if (engine::isDeviceFatalError(rendered.error())) {
                        failThread(rendered.error());
                    } else {
                        failChannel(channel, rendered.error());
                    }
                    continue;
                }
                std::scoped_lock lock(mutex_);
                ++executed_frame_count_;
                publishSurfaceStateLocked(*channel);
            } catch (...) {
                failChannel(channel, threadError(ErrorCode::Internal, "Visual frame execution threw an exception."));
            }
            continue;
        }

        try {
            auto executed = control.execute(*channel->executor);
            if (!executed) {
                if (control.fail) {
                    control.fail(executed.error());
                }
                if (engine::isDeviceFatalError(executed.error())) {
                    // capture 等同步控制即使自身不定义为“通道致命”，DeviceLost 也必须
                    // 立即使共享渲染线程失败，不能等其他 Surface 下一次提交才被动发现。
                    failThread(executed.error());
                } else if (control.fatalOnFailure) {
                    failChannel(channel, executed.error());
                }
                continue;
            }

            {
                std::scoped_lock lock(mutex_);
                ++executed_control_count_;
                if (control.stopsChannel) {
                    channel->lifecycle = Channel::Lifecycle::Stopped;
                    channel->surfaceState = {};
                } else {
                    publishSurfaceStateLocked(*channel);
                    if (control.initializesChannel) {
                        channel->lifecycle = Channel::Lifecycle::Ready;
                    }
                    channel->state.completeResourceBatch(control.resourceBatchId);
                }
            }
            if (control.complete) {
                control.complete();
            }
        } catch (const std::exception& error) {
            LOG_CRITICAL("[RenderThread] Control task failed: {}", error.what());
            const Error failure = threadError(ErrorCode::Internal, "Render control task threw an exception.");
            if (control.fail) {
                control.fail(failure);
            }
            failChannel(channel, failure);
        } catch (...) {
            const Error failure = threadError(ErrorCode::Internal, "Render control task threw an unknown exception.");
            if (control.fail) {
                control.fail(failure);
            }
            failChannel(channel, failure);
        }
    }

    std::vector<std::shared_ptr<Channel>> remaining;
    {
        std::scoped_lock lock(mutex_);
        remaining.reserve(channels_.size());
        for (auto& [id, channel] : channels_) {
            remaining.push_back(channel);
        }
    }
    for (const auto& channel : remaining) {
        channel->executor->shutdown();
    }

    // Device 及其共享 GPU 资源必须在执行线程上完成析构。尤其是 OpenGL，
    // glFinish/glDelete* 依赖当前 WGL Context；若留到 RenderThread 在
    // UI 线程析构成员时再释放，关闭最后一个文档会稳定触发无 Context 的 GL 调用。
    device_context_.reset();
}

}  // namespace mulan::view::detail
