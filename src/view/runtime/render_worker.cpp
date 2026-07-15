#include "runtime/detail/render_worker.h"

#include "runtime/detail/render_executor.h"

#include <mulan/core/log/log.h>

#include <exception>
#include <string_view>
#include <utility>

namespace mulan::view::detail {
namespace {

Error workerError(ErrorCode code, std::string_view message) {
    return Error::make(code, message);
}

}  // namespace

RenderWorker::~RenderWorker() {
    shutdown();
}

bool RenderWorker::isInitialized() const {
    return lifecycle_.load() == Lifecycle::Ready;
}

ResultVoid RenderWorker::initWindow(const ViewConfig& config, int width, int height) {
    return start(
            [config, width, height](RenderExecutor& executor) { return executor.initWindow(config, width, height); });
}

ResultVoid RenderWorker::initOffscreen(const ViewConfig& config, int width, int height) {
    return start([config, width, height](RenderExecutor& executor) {
        return executor.initOffscreen(config, width, height);
    });
}

ResultVoid RenderWorker::start(Initializer initialize) {
    if (lifecycle_.load() == Lifecycle::Ready) {
        return {};
    }
    if (lifecycle_.load() != Lifecycle::Stopped) {
        return std::unexpected(workerError(ErrorCode::InvalidArg, "Render worker is not in the stopped state."));
    }

    {
        std::scoped_lock lock(mutex_);
        controls_.clear();
        latest_frame_.reset();
        protocol_.reset();
        surface_state_ = {};
    }
    lifecycle_.store(Lifecycle::Starting);
    std::promise<ResultVoid> ready;
    auto future = ready.get_future();
    try {
        worker_ = std::jthread(
                [this, initialize = std::move(initialize), ready = std::move(ready)](std::stop_token token) mutable {
                    run(token, std::move(initialize), std::move(ready));
                });
    } catch (const std::exception& error) {
        lifecycle_.store(Lifecycle::Stopped);
        LOG_ERROR("[RenderWorker] Worker thread creation failed: {}", error.what());
        return std::unexpected(workerError(ErrorCode::Internal, "Failed to create the render worker thread."));
    } catch (...) {
        lifecycle_.store(Lifecycle::Stopped);
        LOG_ERROR("[RenderWorker] Worker thread creation failed with an unknown exception");
        return std::unexpected(workerError(ErrorCode::Internal, "Failed to create the render worker thread."));
    }

    try {
        auto result = future.get();
        if (!result) {
            LOG_ERROR("[RenderWorker] Initialization failed: {}", result.error().message);
            shutdown();
        } else {
            LOG_INFO("[RenderWorker] Worker initialized");
        }
        return result;
    } catch (const std::exception& error) {
        LOG_ERROR("[RenderWorker] Initialization handshake failed: {}", error.what());
    } catch (...) {
        LOG_ERROR("[RenderWorker] Initialization handshake failed with an unknown exception");
    }
    shutdown();
    return std::unexpected(workerError(ErrorCode::Internal, "Render worker initialization handshake failed."));
}

void RenderWorker::run(std::stop_token stopToken, Initializer initialize, std::promise<ResultVoid> ready) {
    RenderExecutor executor;
    bool readinessPublished = false;
    try {
        auto initialized = initialize(executor);
        if (!initialized) {
            lifecycle_.store(Lifecycle::Failed);
            ready.set_value(std::move(initialized));
            readinessPublished = true;
        } else {
            // 初始化成功的线性化点：完整表面状态和 Ready 必须先于成功回执可见。
            publishSurfaceState(executor);
            lifecycle_.store(Lifecycle::Ready);
            ready.set_value(ResultVoid{});
            readinessPublished = true;

            while (!stopToken.stop_requested()) {
                ControlTask control;
                bool hasControl = false;
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock, stopToken, [this] {
                        return lifecycle_.load() != Lifecycle::Ready || !controls_.empty() ||
                               hasExecutableFrameLocked();
                    });
                    if (stopToken.stop_requested() || lifecycle_.load() != Lifecycle::Ready) {
                        break;
                    }
                    if (!controls_.empty()) {
                        control = std::move(controls_.front());
                        controls_.pop_front();
                        hasControl = true;
                    }
                }

                if (!hasControl) {
                    auto rendered = executeLatest(executor);
                    if (!rendered) {
                        LOG_CRITICAL("[RenderWorker] Visual frame execution failed: {}", rendered.error().message);
                        failWorker(rendered.error());
                        break;
                    }
                    continue;
                }

                try {
                    auto executed = control.execute(executor);
                    if (!executed) {
                        if (control.fail) {
                            control.fail(executed.error());
                        }
                        if (control.fatalOnFailure) {
                            failWorker(executed.error(), control.resourceSequence, control.resourceBatchId);
                            break;
                        }
                        continue;
                    }
                    publishSurfaceState(executor);
                    if (control.resourceSequence != 0) {
                        bool completed = false;
                        {
                            std::scoped_lock lock(mutex_);
                            completed = protocol_.completeResource(control.resourceSequence, control.resourceBatchId);
                        }
                        if (!completed) {
                            const auto failure =
                                    workerError(ErrorCode::Internal,
                                                "Render resource completion violated reliable queue ordering.");
                            failWorker(failure, control.resourceSequence, control.resourceBatchId);
                            break;
                        }
                    }
                    if (control.complete) {
                        control.complete();
                    }
                } catch (const std::exception& error) {
                    LOG_CRITICAL("[RenderWorker] Control task failed: {}", error.what());
                    const auto failure =
                            workerError(ErrorCode::Internal, "Render worker control task threw an exception.");
                    if (control.fail) {
                        control.fail(failure);
                    }
                    failWorker(failure, control.resourceSequence, control.resourceBatchId);
                    break;
                } catch (...) {
                    LOG_CRITICAL("[RenderWorker] Control task failed with an unknown exception");
                    const auto failure =
                            workerError(ErrorCode::Internal, "Render worker control task threw an unknown exception.");
                    if (control.fail) {
                        control.fail(failure);
                    }
                    failWorker(failure, control.resourceSequence, control.resourceBatchId);
                    break;
                }
            }
        }
    } catch (const std::exception& error) {
        LOG_CRITICAL("[RenderWorker] Worker terminated by exception: {}", error.what());
        const auto failure = workerError(ErrorCode::Internal, "Render worker threw an exception.");
        if (!readinessPublished) {
            lifecycle_.store(Lifecycle::Failed);
            ready.set_value(std::unexpected(failure));
            readinessPublished = true;
        } else {
            failWorker(failure);
        }
    } catch (...) {
        LOG_CRITICAL("[RenderWorker] Worker terminated by an unknown exception");
        const auto failure = workerError(ErrorCode::Internal, "Render worker threw an unknown exception.");
        if (!readinessPublished) {
            lifecycle_.store(Lifecycle::Failed);
            ready.set_value(std::unexpected(failure));
            readinessPublished = true;
        } else {
            failWorker(failure);
        }
    }

    executor.shutdown();
    if (lifecycle_.load() == Lifecycle::Ready) {
        lifecycle_.store(Lifecycle::Stopped);
    }
    LOG_INFO("[RenderWorker] Worker exited");
}

ResultVoid RenderWorker::submitFrame(RenderSubmission submission) {
    {
        std::scoped_lock lock(mutex_);
        if (lifecycle_.load() != Lifecycle::Ready) {
            return std::unexpected(workerError(ErrorCode::InvalidArg, "Render worker is not ready."));
        }
        auto dependency = enqueueSubmissionResourcesLocked(submission);
        if (!dependency) {
            return std::unexpected(dependency.error());
        }
        latest_frame_ = PendingFrame{
            .submission = std::move(submission),
            .requiredResourceSequence = *dependency,
        };
    }
    wake_.notify_one();
    return {};
}

Result<uint64_t> RenderWorker::enqueueSubmissionResourcesLocked(RenderSubmission& submission) {
    const bool hasPrepare = !submission.prepare.empty();
    const bool hasBatch = submission.resourceBatchId != 0;
    if (hasPrepare != hasBatch) {
        return std::unexpected(workerError(
                ErrorCode::InvalidArg,
                "Render submission resource batch id and prepare payload must either both exist or both be empty."));
    }

    if (hasPrepare) {
        const ResourceRegistration registration = protocol_.registerResourceBatch(submission.resourceBatchId);
        if (registration.newlyQueued) {
            // 新资源可能 force-update 既有 GPU key；丢弃旧世界帧，防止它在上传后执行。
            latest_frame_.reset();
            engine::RenderResourcePrepareList prepare = std::move(submission.prepare);
            controls_.push_back(ControlTask{
                    .execute = [prepare = std::move(prepare)](
                                       RenderExecutor& executor) { return executor.prepareResources(prepare); },
                    .fatalOnFailure = true,
                    .resourceSequence = registration.sequence,
                    .resourceBatchId = submission.resourceBatchId,
            });
        } else {
            // builder 在 ACK 前会重复携带同一完整批次；可靠队列已经拥有它。
            submission.prepare.clear();
        }
    }

    submission.prepare.clear();
    return protocol_.currentDependency();
}

bool RenderWorker::hasExecutableFrameLocked() const {
    return latest_frame_.has_value() && protocol_.canExecuteFrame(latest_frame_->requiredResourceSequence);
}

ResultVoid RenderWorker::executeLatest(RenderExecutor& executor) {
    std::optional<PendingFrame> pending;
    {
        std::scoped_lock lock(mutex_);
        if (!hasExecutableFrameLocked()) {
            return {};
        }
        pending = std::move(latest_frame_);
        latest_frame_.reset();
    }
    if (!pending) {
        return {};
    }
    return executor.executeFrame(pending->submission);
}

Result<engine::RenderCaptureResult> RenderWorker::capture(RenderSubmission submission, engine::RenderCaptureDesc desc) {
    if (worker_.joinable() && worker_.get_id() == std::this_thread::get_id()) {
        return std::unexpected(workerError(ErrorCode::InvalidArg, "Capture cannot wait on the render worker thread."));
    }

    using CaptureResult = Result<engine::RenderCaptureResult>;
    auto promise = std::make_shared<std::promise<CaptureResult>>();
    auto outcome = std::make_shared<std::optional<CaptureResult>>();
    auto future = promise->get_future();
    {
        std::scoped_lock lock(mutex_);
        if (lifecycle_.load() != Lifecycle::Ready) {
            return std::unexpected(workerError(ErrorCode::InvalidArg, "Render worker is not available."));
        }
        auto dependency = enqueueSubmissionResourcesLocked(submission);
        if (!dependency) {
            return std::unexpected(dependency.error());
        }
        controls_.push_back(ControlTask{
                .execute = [submission = std::move(submission), desc,
                            outcome](RenderExecutor& executor) mutable -> ResultVoid {
                    *outcome = executor.capture(submission, desc);
                    return {};
                },
                .complete = [promise, outcome] { promise->set_value(std::move(outcome->value())); },
                .fail = [promise](const Error& error) { promise->set_value(std::unexpected(error)); },
        });
    }
    wake_.notify_one();
    return future.get();
}

Result<RenderSurfaceState> RenderWorker::resize(int width, int height) {
    if (worker_.joinable() && worker_.get_id() == std::this_thread::get_id()) {
        return std::unexpected(workerError(ErrorCode::InvalidArg, "Resize cannot wait on the render worker thread."));
    }
    if (width <= 0 || height <= 0) {
        return std::unexpected(workerError(ErrorCode::InvalidArg, "Resize dimensions must be positive."));
    }

    using ResizeResult = Result<RenderSurfaceState>;
    auto promise = std::make_shared<std::promise<ResizeResult>>();
    auto outcome = std::make_shared<std::optional<ResizeResult>>();
    auto future = promise->get_future();
    {
        std::scoped_lock lock(mutex_);
        if (lifecycle_.load() != Lifecycle::Ready) {
            return std::unexpected(workerError(ErrorCode::InvalidArg, "Render worker is not available."));
        }
        // 交换链 resize 可能先破坏旧后备缓冲再失败；在同一线性化点丢弃旧视觉帧，
        // 并让真实 resize 失败直接终止 worker，禁止半失效表面继续执行。
        latest_frame_.reset();
        controls_.push_back(ControlTask{
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

void RenderWorker::enableIBL(std::string hdrPath) {
    if (!enqueue(ControlTask{
                .execute = [path = std::move(hdrPath)](RenderExecutor& executor) -> ResultVoid {
                    executor.enableIBL(path);
                    return {};
                },
        })) {
        LOG_WARN("[RenderWorker] IBL request ignored because the worker is not ready");
    }
}

ResultVoid RenderWorker::clearAssetResources() {
    {
        std::scoped_lock lock(mutex_);
        if (lifecycle_.load() != Lifecycle::Ready) {
            return std::unexpected(workerError(ErrorCode::InvalidArg, "Render worker is not ready."));
        }
        // 文档资源域切换时，旧文档邮箱必须与 GPU 缓存一起失效。
        latest_frame_.reset();
        const uint64_t sequence = protocol_.registerResourceBarrier();
        controls_.push_back(ControlTask{
                .execute = [](RenderExecutor& executor) -> ResultVoid {
                    executor.clearAssetResources();
                    return {};
                },
                .fatalOnFailure = true,
                .resourceSequence = sequence,
        });
    }
    wake_.notify_one();
    return {};
}

std::vector<RenderWorkerEvent> RenderWorker::drainEvents() {
    std::scoped_lock lock(mutex_);
    return protocol_.drainEvents();
}

std::optional<Error> RenderWorker::failureSnapshot() const {
    std::scoped_lock lock(mutex_);
    return protocol_.failure();
}

bool RenderWorker::enqueue(ControlTask task) {
    {
        std::scoped_lock lock(mutex_);
        if (lifecycle_.load() != Lifecycle::Ready) {
            return false;
        }
        controls_.push_back(std::move(task));
    }
    wake_.notify_one();
    return true;
}

void RenderWorker::shutdown() {
    const bool hadWorker = worker_.joinable() || lifecycle_.load() != Lifecycle::Stopped;
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        if (!hadWorker) {
            return;
        }
        lifecycle_.store(Lifecycle::Stopping);
        latest_frame_.reset();
        cancelled = std::move(controls_);
    }

    const auto cancellation = workerError(ErrorCode::InvalidArg, "Render request was cancelled during shutdown.");
    for (auto& control : cancelled) {
        if (control.fail) {
            control.fail(cancellation);
        }
    }

    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }

    {
        std::scoped_lock lock(mutex_);
        surface_state_ = {};
        protocol_.reset();
    }
    lifecycle_.store(Lifecycle::Stopped);
    LOG_INFO("[RenderWorker] Worker shut down");
}

RenderSurfaceState RenderWorker::surfaceState() const {
    std::scoped_lock lock(mutex_);
    return surface_state_;
}

void RenderWorker::publishSurfaceState(const RenderExecutor& executor) {
    const RenderSurfaceState state = executor.surfaceState();
    std::scoped_lock lock(mutex_);
    surface_state_ = state;
}

void RenderWorker::failWorker(const Error& error, uint64_t resourceSequence, uint64_t resourceBatchId) {
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        lifecycle_.store(Lifecycle::Failed);
        latest_frame_.reset();
        cancelled = std::move(controls_);
        protocol_.fail(error, resourceSequence, resourceBatchId);
    }
    for (auto& control : cancelled) {
        if (control.fail) {
            control.fail(error);
        }
    }
    wake_.notify_all();
}

}  // namespace mulan::view::detail
