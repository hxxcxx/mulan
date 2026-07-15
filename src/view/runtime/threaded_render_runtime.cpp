#include <mulan/view/runtime/threaded_render_runtime.h>

#include <mulan/core/log/log.h>

#include <exception>
#include <future>

namespace mulan::view {

ThreadedRenderRuntime::~ThreadedRenderRuntime() {
    shutdown();
}

core::Result<void> ThreadedRenderRuntime::initWindow(const ViewConfig& config, int width, int height) {
    return start([config, width, height](RenderRuntime& runtime) { return runtime.initWindow(config, width, height); });
}

core::Result<void> ThreadedRenderRuntime::initOffscreen(const ViewConfig& config, int width, int height) {
    return start(
            [config, width, height](RenderRuntime& runtime) { return runtime.initOffscreen(config, width, height); });
}

core::Result<void> ThreadedRenderRuntime::initOffscreen(int width, int height) {
    return initOffscreen(ViewConfig{}, width, height);
}

core::Result<void> ThreadedRenderRuntime::start(std::function<core::Result<void>(RenderRuntime&)> initialize) {
    if (initialized_.load())
        return {};
    std::promise<core::Result<void>> ready;
    auto future = ready.get_future();
    worker_ = std::jthread(
            [this, initialize = std::move(initialize), ready = std::move(ready)](std::stop_token token) mutable {
                run(token, std::move(initialize), std::move(ready));
            });
    auto result = future.get();
    if (!result) {
        LOG_ERROR("[ThreadedRenderRuntime] Initialization failed: {}", result.error().message);
        shutdown();
    } else {
        LOG_INFO("[ThreadedRenderRuntime] Worker initialized");
    }
    return result;
}

void ThreadedRenderRuntime::run(std::stop_token stopToken, std::function<core::Result<void>(RenderRuntime&)> initialize,
                                std::promise<core::Result<void>> ready) {
    RenderRuntime runtime;
    bool readinessPublished = false;
    try {
        auto initialized = initialize(runtime);
        ready.set_value(initialized);
        readinessPublished = true;
        if (!initialized)
            return;
        publishSurfaceState(runtime);
        initialized_.store(true);
        while (!stopToken.stop_requested()) {
            ControlTask control;
            bool hasControl = false;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, stopToken,
                           [this] { return closing_ || !controls_.empty() || latest_frame_.has_value(); });
                if (closing_)
                    break;
                if (!controls_.empty()) {
                    control = std::move(controls_.front());
                    controls_.pop_front();
                    hasControl = true;
                }
            }
            if (hasControl) {
                if (control.flushFrame)
                    renderLatest(runtime);
                control.execute(runtime);
                publishSurfaceState(runtime);
            } else {
                renderLatest(runtime);
            }
        }
    } catch (const std::exception& error) {
        LOG_CRITICAL("[ThreadedRenderRuntime] Worker terminated by exception: {}", error.what());
        if (!readinessPublished) {
            ready.set_value(std::unexpected(
                    core::Error::make(core::ErrorCode::Internal, "Render worker initialization threw an exception.")));
        }
    } catch (...) {
        LOG_CRITICAL("[ThreadedRenderRuntime] Worker terminated by an unknown exception");
        if (!readinessPublished) {
            ready.set_value(std::unexpected(core::Error::make(
                    core::ErrorCode::Internal, "Render worker initialization threw an unknown exception.")));
        }
    }
    runtime.shutdown();
    initialized_.store(false);
    LOG_INFO("[ThreadedRenderRuntime] Worker exited");
}

void ThreadedRenderRuntime::renderLatest(RenderRuntime& runtime) {
    std::optional<RenderSubmission> submission;
    {
        std::scoped_lock lock(mutex_);
        submission = std::move(latest_frame_);
        latest_frame_.reset();
    }
    if (!submission || submission->surfaceGeneration != surfaceGeneration())
        return;
    runtime.render(*submission);
}

void ThreadedRenderRuntime::submitFrame(RenderSubmission submission) {
    {
        std::scoped_lock lock(mutex_);
        if (closing_)
            return;
        latest_frame_ = std::move(submission);
    }
    wake_.notify_one();
}

core::Result<engine::RenderCaptureResult> ThreadedRenderRuntime::capture(RenderSubmission submission,
                                                                         engine::RenderCaptureDesc desc) {
    if (worker_.joinable() && worker_.get_id() == std::this_thread::get_id()) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "Capture cannot synchronously wait on render thread."));
    }
    auto promise = std::make_shared<std::promise<core::Result<engine::RenderCaptureResult>>>();
    auto future = promise->get_future();
    {
        std::scoped_lock lock(mutex_);
        if (closing_ || !initialized_.load()) {
            return std::unexpected(
                    core::Error::make(core::ErrorCode::InvalidArg, "Threaded render runtime is not available."));
        }
        controls_.push_back({
                [submission = std::move(submission), desc, promise](RenderRuntime& runtime) mutable {
                    promise->set_value(runtime.capture(submission, desc));
                },
                true,
                [promise] {
                    promise->set_value(std::unexpected(
                            core::Error::make(core::ErrorCode::InvalidArg, "Capture was cancelled during shutdown.")));
                },
        });
    }
    wake_.notify_one();
    return future.get();
}

bool ThreadedRenderRuntime::enqueue(ControlTask task) {
    {
        std::scoped_lock lock(mutex_);
        if (closing_)
            return false;
        controls_.push_back(std::move(task));
    }
    wake_.notify_one();
    return true;
}

void ThreadedRenderRuntime::resize(int width, int height) {
    enqueue({ [width, height](RenderRuntime& runtime) { runtime.resize(width, height); } });
}
void ThreadedRenderRuntime::enableIBL(std::string hdrPath) {
    enqueue({ [path = std::move(hdrPath)](RenderRuntime& runtime) { runtime.enableIBL(path); } });
}
void ThreadedRenderRuntime::clearAssetResources() {
    enqueue({ [](RenderRuntime& runtime) { runtime.clearAssetResources(); } });
}
void ThreadedRenderRuntime::shutdown() {
    const bool hadWorker = worker_.joinable() || initialized_.load();
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        if (closing_)
            return;
        closing_ = true;
        latest_frame_.reset();
        cancelled = std::move(controls_);
    }
    for (auto& control : cancelled) {
        if (control.cancel)
            control.cancel();
    }
    wake_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
        worker_.join();
    initialized_.store(false);
    if (hadWorker) {
        LOG_INFO("[ThreadedRenderRuntime] Shut down");
    }
}
uint32_t ThreadedRenderRuntime::surfaceWidth() const {
    std::scoped_lock lock(mutex_);
    return surface_width_;
}
uint32_t ThreadedRenderRuntime::surfaceHeight() const {
    std::scoped_lock lock(mutex_);
    return surface_height_;
}
uint64_t ThreadedRenderRuntime::surfaceGeneration() const {
    std::scoped_lock lock(mutex_);
    return surface_generation_;
}
void ThreadedRenderRuntime::publishSurfaceState(const RenderRuntime& runtime) {
    std::scoped_lock lock(mutex_);
    surface_width_ = runtime.surface().width();
    surface_height_ = runtime.surface().height();
    surface_generation_ = runtime.surface().generation();
}

}  // namespace mulan::view
