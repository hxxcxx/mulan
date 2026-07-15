/**
 * @file render_worker.h
 * @brief RenderWorker 在专用线程上独占 RenderExecutor，并区分可靠控制与可覆盖帧。
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include "render_surface_state.h"

#include "scene_sync/render_submission.h"

#include <mulan/core/result/error.h>
#include <mulan/render/frontend/render_capture.h>
#include <mulan/render/frontend/render_resource_prepare.h>
#include <mulan/view/core/view_config.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace mulan::view::detail {

class RenderExecutor;

class RenderWorker {
public:
    RenderWorker() = default;
    ~RenderWorker();

    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    core::Result<void> initWindow(const ViewConfig& config, int width, int height);
    core::Result<void> initOffscreen(const ViewConfig& config, int width, int height);
    void shutdown();

    bool isInitialized() const;

    core::Result<void> prepareResources(engine::RenderResourcePrepareList prepare);
    core::Result<void> submitFrame(RenderSubmission submission);
    core::Result<engine::RenderCaptureResult> capture(RenderSubmission submission, engine::RenderCaptureDesc desc);
    core::Result<RenderSurfaceState> resize(int width, int height);
    void enableIBL(std::string hdrPath);
    core::Result<void> clearAssetResources();

    RenderSurfaceState surfaceState() const;

private:
    enum class Lifecycle : uint8_t {
        Stopped,
        Starting,
        Ready,
        Stopping,
        Failed,
    };

    struct ControlTask {
        std::function<core::Result<void>(RenderExecutor&)> execute;
        bool fatalOnFailure = false;
        std::function<void()> complete;
        std::function<void(const core::Error&)> fail;
    };

    using Initializer = std::function<core::Result<void>(RenderExecutor&)>;

    core::Result<void> start(Initializer initialize);
    void run(std::stop_token stopToken, Initializer initialize, std::promise<core::Result<void>> ready);
    bool enqueue(ControlTask task);
    core::Result<void> executeLatest(RenderExecutor& executor);
    void publishSurfaceState(const RenderExecutor& executor);
    void failWorker(const core::Error& error);

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<ControlTask> controls_;
    std::optional<RenderSubmission> latest_frame_;
    std::jthread worker_;
    std::atomic<Lifecycle> lifecycle_ = Lifecycle::Stopped;
    RenderSurfaceState surface_state_;
};

}  // namespace mulan::view::detail
