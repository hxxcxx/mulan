/**
 * @file render_thread.cpp
 * @brief RenderThread 的多 Surface 公平调度与通道状态实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "detail/render_thread.h"

#include "detail/render_executor.h"
#include "detail/render_device_context.h"

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

void notifyChannelEvent(const RenderChannelEventCallback& callback) noexcept {
    if (!callback) {
        return;
    }
    try {
        callback();
    } catch (const std::exception& error) {
        LOG_ERROR("[RenderThread] Channel event callback failed: {}", error.what());
    } catch (...) {
        LOG_ERROR("[RenderThread] Channel event callback failed with an unknown exception");
    }
}

}  // namespace

struct RenderThread::ControlTask {
    enum class Kind : uint8_t {
        Command,
        Initialize,
        Shutdown,
        ResourcePrepare,
    };

    enum class FailurePolicy : uint8_t {
        Request,
        Channel,
    };

    std::function<ResultVoid(Channel&)> run;
    /// 在任务成功、失败、异常或取消后恰好结算一次；空回调表示 fire-and-forget。
    std::function<void(ResultVoid)> finish;
    Kind kind = Kind::Command;
    FailurePolicy failurePolicy = FailurePolicy::Request;
    uint64_t resourceBatchId = 0;
};

struct RenderThread::Channel {
    enum class Lifecycle : uint8_t {
        Starting,
        Ready,
        Stopping,
        Stopped,
        Failed,
    };

    Channel(RenderChannelId value, RenderChannelEventCallback callback)
        : id(value), eventCallback(std::move(callback)) {}

    RenderChannelId id = 0;
    std::unique_ptr<RenderExecutor> executor;
    std::deque<ControlTask> controls;
    std::optional<RenderSubmission> latestFrame;
    RenderChannelState state;
    RenderSurfaceState surfaceState;
    Lifecycle lifecycle = Lifecycle::Starting;
    RenderChannelEventCallback eventCallback;
};

Error RenderThread::threadError(ErrorCode code, std::string_view message) {
    return Error::make(code, message);
}

Result<std::shared_ptr<RenderThread>> RenderThread::acquire(const RenderDeviceConfig& config) {
    MULAN_PROFILE_ZONE();

    std::scoped_lock registryLock(registry_mutex);
    if (config.backend != engine::GraphicsBackend::OpenGL) {
        for (auto it = registry.begin(); it != registry.end();) {
            if (auto thread = it->lock()) {
                if (thread->isHealthy() && thread->config_.sharesExecutionThreadWith(config)) {
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

RenderThread::RenderThread(const RenderDeviceConfig& config)
    : config_(config), thread_id_(next_thread_id.fetch_add(1)) {
    thread_ = std::jthread([this](std::stop_token token) { run(token); });
    {
        std::scoped_lock lock(mutex_);
        state_ = State::Healthy;
    }
    LOG_INFO("[RenderThread] Thread created: id={}, backend={}", thread_id_, static_cast<int>(config.backend));
}

RenderThread::~RenderThread() {
    {
        std::scoped_lock lock(mutex_);
        assert(channels_.empty() && "RenderThread must not be destroyed with attached RenderChannels.");
        stopping_ = true;
        state_ = State::Stopped;
    }
    thread_.request_stop();
    wake_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
    LOG_INFO("[RenderThread] Thread destroyed: id={}", thread_id_);
}

Result<RenderChannelId> RenderThread::attachChannel(const RenderSurfaceConfig& config, int width, int height,
                                                    RenderChannelEventCallback eventCallback) {
    MULAN_PROFILE_ZONE();

    return createChannel(
            [this, config, width, height](Channel& channel) -> ResultVoid {
                return initializeChannel(channel, config, width, height);
            },
            std::move(eventCallback));
}

ResultVoid RenderThread::initializeChannel(Channel& channel, const RenderSurfaceConfig& config, int width, int height) {
    MULAN_PROFILE_ZONE();

    if (auto ensured = ensureDeviceContext(); !ensured) {
        return ensured;
    }

    auto executor = std::make_unique<RenderExecutor>(*device_context_);
    if (auto initialized = executor->init(config, width, height); !initialized) {
        return initialized;
    }

    channel.executor = std::move(executor);
    return {};
}

ResultVoid RenderThread::ensureDeviceContext() {
    MULAN_PROFILE_ZONE();

    if (device_context_)
        return {};
    auto created = RenderDeviceContext::create(config_);
    if (!created)
        return std::unexpected(created.error());
    device_context_ = std::move(*created);
    return {};
}

Result<RenderChannelId> RenderThread::createChannel(Initializer initialize, RenderChannelEventCallback eventCallback) {
    MULAN_PROFILE_ZONE();

    auto promise = std::make_shared<std::promise<ResultVoid>>();
    auto future = promise->get_future();
    RenderChannelId channelId = 0;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_ || state_ != State::Healthy) {
            return std::unexpected(threadError(ErrorCode::InvalidArg, "Render thread is stopping."));
        }
        channelId = next_channel_++;
        if (channelId == 0) {
            channelId = next_channel_++;
        }
        auto channel = std::make_unique<Channel>(channelId, std::move(eventCallback));
        channel->controls.push_back(ControlTask{
                .run = std::move(initialize),
                .finish = [promise](ResultVoid result) { promise->set_value(std::move(result)); },
                .kind = ControlTask::Kind::Initialize,
                .failurePolicy = ControlTask::FailurePolicy::Channel,
        });
        channels_.push_back(std::move(channel));
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
        Channel* channel = findChannelLocked(channelId);
        if (!channel) {
            return;
        }
        if (channel->lifecycle == Channel::Lifecycle::Stopped) {
            promise->set_value();
        } else {
            channel->lifecycle = Channel::Lifecycle::Stopping;
            channel->latestFrame.reset();
            cancelled = std::move(channel->controls);
            channel->controls.push_back(ControlTask{
                    .run = [](Channel& channel) -> ResultVoid {
                        if (channel.executor) {
                            channel.executor->shutdown();
                            channel.executor.reset();
                        }
                        return {};
                    },
                    .finish = [promise](ResultVoid) { promise->set_value(); },
                    .kind = ControlTask::Kind::Shutdown,
            });
        }
    }
    const Error cancellation = threadError(ErrorCode::InvalidArg, "Render request was cancelled during shutdown.");
    for (ControlTask& task : cancelled) {
        finishControlTask(task, std::unexpected(cancellation));
    }
    wake_.notify_one();
    future.wait();

    {
        std::scoped_lock lock(mutex_);
        std::erase_if(channels_, [channelId](const auto& channel) { return channel->id == channelId; });
        next_channel_index_ = channels_.empty() ? 0 : next_channel_index_ % channels_.size();
    }
}

bool RenderThread::isReady(RenderChannelId channelId) const {
    std::scoped_lock lock(mutex_);
    const Channel* channel = findChannelLocked(channelId);
    return state_ == State::Healthy && channel && channel->lifecycle == Channel::Lifecycle::Ready;
}

ResultVoid RenderThread::submitFrame(RenderChannelId channelId, RenderSubmission submission) {
    {
        std::scoped_lock lock(mutex_);
        Channel* channel = findChannelLocked(channelId);
        if (state_ != State::Healthy || !channel || channel->lifecycle != Channel::Lifecycle::Ready) {
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not ready.")));
        }
        auto enqueued = enqueueSubmissionResourcesLocked(*channel, submission);
        if (!enqueued) {
            return std::unexpected(enqueued.error());
        }
        channel->latestFrame = std::move(submission);
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
                    .run = [prepare = std::move(prepare)](
                                   Channel& channel) { return channel.executor->prepareResources(prepare); },
                    .kind = ControlTask::Kind::ResourcePrepare,
                    .failurePolicy = ControlTask::FailurePolicy::Channel,
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
        Channel* channel = findChannelLocked(channelId);
        if (state_ != State::Healthy || !channel || channel->lifecycle != Channel::Lifecycle::Ready) {
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not available.")));
        }
        auto enqueued = enqueueSubmissionResourcesLocked(*channel, submission);
        if (!enqueued) {
            return std::unexpected(enqueued.error());
        }
        channel->controls.push_back(ControlTask{
                .run = [submission = std::move(submission), desc, outcome](Channel& channel) mutable -> ResultVoid {
                    *outcome = channel.executor->capture(submission, desc);
                    if (!outcome->value()) {
                        return std::unexpected(outcome->value().error());
                    }
                    return {};
                },
                .finish =
                        [promise, outcome](ResultVoid result) {
                            if (!result) {
                                promise->set_value(std::unexpected(result.error()));
                                return;
                            }
                            promise->set_value(std::move(outcome->value()));
                        },
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
        Channel* channel = findChannelLocked(channelId);
        if (state_ != State::Healthy || !channel || channel->lifecycle != Channel::Lifecycle::Ready) {
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not available.")));
        }
        channel->latestFrame.reset();
        channel->controls.push_back(ControlTask{
                .run = [width, height, outcome](Channel& channel) -> ResultVoid {
                    *outcome = channel.executor->resize(width, height);
                    if (!outcome->value()) {
                        return std::unexpected(outcome->value().error());
                    }
                    return {};
                },
                .finish =
                        [promise, outcome](ResultVoid result) {
                            if (!result) {
                                promise->set_value(std::unexpected(result.error()));
                                return;
                            }
                            promise->set_value(std::move(outcome->value()));
                        },
                .failurePolicy = ControlTask::FailurePolicy::Channel,
        });
    }
    wake_.notify_one();
    return future.get();
}

void RenderThread::enableIBL(RenderChannelId channelId, std::string hdrPath) {
    if (!enqueue(channelId, ControlTask{
                                    .run = [path = std::move(hdrPath)](Channel& channel) -> ResultVoid {
                                        channel.executor->enableIBL(path);
                                        return {};
                                    },
                            })) {
        LOG_WARN("[RenderThread] IBL request ignored because the channel is not ready");
    }
}

ResultVoid RenderThread::clearAssetResources(RenderChannelId channelId) {
    {
        std::scoped_lock lock(mutex_);
        Channel* channel = findChannelLocked(channelId);
        if (state_ != State::Healthy || !channel || channel->lifecycle != Channel::Lifecycle::Ready) {
            return std::unexpected(
                    thread_failure_.value_or(threadError(ErrorCode::InvalidArg, "Render channel is not ready.")));
        }
        channel->latestFrame.reset();
        channel->state.invalidateResourceBatch();
        channel->controls.push_back(ControlTask{
                .run = [](Channel& channel) -> ResultVoid { return channel.executor->clearAssetResources(); },
                .failurePolicy = ControlTask::FailurePolicy::Channel,
        });
    }
    wake_.notify_one();
    return {};
}

bool RenderThread::enqueue(RenderChannelId channelId, ControlTask task) {
    {
        std::scoped_lock lock(mutex_);
        Channel* channel = findChannelLocked(channelId);
        if (state_ != State::Healthy || !channel || channel->lifecycle != Channel::Lifecycle::Ready) {
            return false;
        }
        channel->controls.push_back(std::move(task));
    }
    wake_.notify_one();
    return true;
}

std::optional<uint64_t> RenderThread::takeCompletedResourceBatch(RenderChannelId channelId) {
    std::scoped_lock lock(mutex_);
    Channel* channel = findChannelLocked(channelId);
    return channel ? channel->state.takeCompletedResourceBatch() : std::nullopt;
}

std::optional<Error> RenderThread::failureSnapshot(RenderChannelId channelId) const {
    std::scoped_lock lock(mutex_);
    const Channel* channel = findChannelLocked(channelId);
    return channel ? channel->state.failure() : std::nullopt;
}

RenderSurfaceState RenderThread::surfaceState(RenderChannelId channelId) const {
    std::scoped_lock lock(mutex_);
    const Channel* channel = findChannelLocked(channelId);
    return channel ? channel->surfaceState : RenderSurfaceState{};
}

bool RenderThread::channelHasWorkLocked(const Channel& channel) const {
    if (!channel.controls.empty()) {
        return true;
    }
    return channel.lifecycle == Channel::Lifecycle::Ready && channel.latestFrame.has_value();
}

RenderThread::Channel* RenderThread::findChannelLocked(RenderChannelId channelId) {
    const auto found = std::find_if(channels_.begin(), channels_.end(),
                                    [channelId](const auto& channel) { return channel->id == channelId; });
    return found == channels_.end() ? nullptr : found->get();
}

const RenderThread::Channel* RenderThread::findChannelLocked(RenderChannelId channelId) const {
    const auto found = std::find_if(channels_.begin(), channels_.end(),
                                    [channelId](const auto& channel) { return channel->id == channelId; });
    return found == channels_.end() ? nullptr : found->get();
}

bool RenderThread::isHealthy() const {
    std::scoped_lock lock(mutex_);
    return state_ == State::Healthy;
}

bool RenderThread::hasWorkLocked() const {
    return std::any_of(channels_.begin(), channels_.end(),
                       [this](const auto& channel) { return channelHasWorkLocked(*channel); });
}

RenderThread::Channel* RenderThread::selectReadyChannelLocked(std::optional<ControlTask>& control,
                                                              std::optional<RenderSubmission>& frame) {
    if (channels_.empty()) {
        return nullptr;
    }
    const size_t count = channels_.size();
    const size_t start = next_channel_index_ % count;
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t index = (start + offset) % count;
        Channel& channel = *channels_[index];
        if (!channelHasWorkLocked(channel)) {
            continue;
        }
        next_channel_index_ = (index + 1) % count;
        if (!channel.controls.empty()) {
            control.emplace(std::move(channel.controls.front()));
            channel.controls.pop_front();
        } else {
            frame = std::move(channel.latestFrame);
            channel.latestFrame.reset();
        }
        return &channel;
    }
    return nullptr;
}

void RenderThread::publishSurfaceStateLocked(Channel& channel) {
    channel.surfaceState = channel.executor ? channel.executor->surfaceState() : RenderSurfaceState{};
}

void RenderThread::failChannel(Channel& channel, const Error& error) {
    std::deque<ControlTask> cancelled;
    RenderChannelEventCallback eventCallback;
    {
        std::scoped_lock lock(mutex_);
        channel.latestFrame.reset();
        channel.state.fail(error);
        if (channel.lifecycle == Channel::Lifecycle::Stopping) {
            return;
        }
        channel.lifecycle = Channel::Lifecycle::Failed;
        cancelled = std::move(channel.controls);
        eventCallback = channel.eventCallback;
    }
    for (ControlTask& task : cancelled) {
        finishControlTask(task, std::unexpected(error));
    }
    notifyChannelEvent(eventCallback);
    wake_.notify_all();
}

void RenderThread::failThread(const Error& error) {
    std::deque<ControlTask> cancelled;
    std::vector<RenderChannelEventCallback> eventCallbacks;
    {
        std::scoped_lock lock(mutex_);
        if (state_ == State::Failed || state_ == State::Stopped) {
            return;
        }
        state_ = State::Failed;
        thread_failure_ = error;
        for (auto& channel : channels_) {
            channel->latestFrame.reset();
            channel->state.fail(error);
            if (channel->lifecycle == Channel::Lifecycle::Stopping) {
                continue;
            }
            channel->lifecycle = Channel::Lifecycle::Failed;
            if (channel->eventCallback) {
                eventCallbacks.push_back(channel->eventCallback);
            }
            while (!channel->controls.empty()) {
                cancelled.push_back(std::move(channel->controls.front()));
                channel->controls.pop_front();
            }
        }
    }
    for (ControlTask& task : cancelled) {
        finishControlTask(task, std::unexpected(error));
    }
    for (const RenderChannelEventCallback& callback : eventCallbacks) {
        notifyChannelEvent(callback);
    }
    LOG_CRITICAL("[RenderThread] Shared device failed: threadId={}, error={}", thread_id_, error.message);
    wake_.notify_all();
}

void RenderThread::executeFrame(Channel& channel, RenderSubmission submission) {
    try {
        if (!channel.executor) {
            failChannel(channel, threadError(ErrorCode::Internal, "Ready render channel has no executor."));
            return;
        }
        MULAN_PROFILE_FRAME();
        auto rendered = [&] {
            MULAN_PROFILE_ZONE_N("RenderThread/ExecuteFrame");
            return channel.executor->executeFrame(submission);
        }();
        if (!rendered) {
            if (engine::isDeviceFatalError(rendered.error())) {
                failThread(rendered.error());
            } else {
                failChannel(channel, rendered.error());
            }
            return;
        }
        std::scoped_lock lock(mutex_);
        publishSurfaceStateLocked(channel);
    } catch (...) {
        failChannel(channel, threadError(ErrorCode::Internal, "Visual frame execution threw an exception."));
    }
}

void RenderThread::finishControlTask(ControlTask& control, ResultVoid result) noexcept {
    auto finish = std::move(control.finish);
    if (!finish) {
        return;
    }
    try {
        finish(std::move(result));
    } catch (const std::exception& error) {
        LOG_ERROR("[RenderThread] Control task completion callback failed: {}", error.what());
    } catch (...) {
        LOG_ERROR("[RenderThread] Control task completion callback failed with an unknown exception");
    }
}

void RenderThread::executeControl(Channel& channel, ControlTask control) {
    try {
        ResultVoid executed;
        {
            MULAN_PROFILE_ZONE_N("RenderThread/ControlTask");
            executed = control.run(channel);
        }

        if (!executed) {
            finishControlTask(control, std::unexpected(executed.error()));
            if (engine::isDeviceFatalError(executed.error())) {
                // 同步控制即使只影响请求，DeviceLost 也必须广播到共享 Device 的所有通道。
                failThread(executed.error());
            } else if (control.failurePolicy == ControlTask::FailurePolicy::Channel) {
                failChannel(channel, executed.error());
            }
            return;
        }

        bool resourceBatchCompleted = false;
        {
            std::scoped_lock lock(mutex_);
            switch (control.kind) {
            case ControlTask::Kind::Shutdown:
                channel.lifecycle = Channel::Lifecycle::Stopped;
                channel.surfaceState = {};
                break;
            case ControlTask::Kind::Initialize:
                publishSurfaceStateLocked(channel);
                channel.lifecycle = Channel::Lifecycle::Ready;
                break;
            case ControlTask::Kind::ResourcePrepare:
                publishSurfaceStateLocked(channel);
                channel.state.completeResourceBatch(control.resourceBatchId);
                resourceBatchCompleted = true;
                break;
            case ControlTask::Kind::Command: publishSurfaceStateLocked(channel); break;
            }
        }
        finishControlTask(control, ResultVoid{});
        if (resourceBatchCompleted) {
            notifyChannelEvent(channel.eventCallback);
        }
    } catch (const std::exception& error) {
        LOG_CRITICAL("[RenderThread] Control task failed: {}", error.what());
        const Error failure = threadError(ErrorCode::Internal, "Render control task threw an exception.");
        finishControlTask(control, std::unexpected(failure));
        failChannel(channel, failure);
    } catch (...) {
        const Error failure = threadError(ErrorCode::Internal, "Render control task threw an unknown exception.");
        finishControlTask(control, std::unexpected(failure));
        failChannel(channel, failure);
    }
}

void RenderThread::run(std::stop_token stopToken) {
    MULAN_PROFILE_THREAD("RenderThread");

    while (!stopToken.stop_requested()) {
        std::optional<ControlTask> control;
        std::optional<RenderSubmission> frame;
        Channel* channel = nullptr;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stopToken, [this] { return stopping_ || hasWorkLocked(); });
            if (stopToken.stop_requested() || stopping_) {
                break;
            }
            channel = selectReadyChannelLocked(control, frame);
        }
        if (!channel) {
            continue;
        }
        if (control) {
            executeControl(*channel, std::move(*control));
        } else if (frame) {
            executeFrame(*channel, std::move(*frame));
        }
    }

    for (const auto& channel : channels_) {
        if (channel->executor) {
            channel->executor->shutdown();
            channel->executor.reset();
        }
    }

    // Device 及其共享 GPU 资源必须在执行线程上完成析构。尤其是 OpenGL，
    // glFinish/glDelete* 依赖当前 WGL Context；若留到 RenderThread 在
    // UI 线程析构成员时再释放，关闭最后一个文档会稳定触发无 Context 的 GL 调用。
    device_context_.reset();
}

}  // namespace mulan::view::detail
