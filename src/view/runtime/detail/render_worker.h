/**
 * @file render_worker.h
 * @brief RenderWorker 在专用线程上独占 RenderExecutor，并区分可靠控制与可覆盖帧。
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include "render_worker_protocol.h"
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
#include <vector>

namespace mulan::view::detail {

class RenderExecutor;

class RenderWorker {
public:
    RenderWorker() = default;
    ~RenderWorker();

    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    ResultVoid initWindow(const ViewConfig& config, int width, int height);
    ResultVoid initOffscreen(const ViewConfig& config, int width, int height);
    void shutdown();

    bool isInitialized() const;

    ResultVoid submitFrame(RenderSubmission submission);
    Result<engine::RenderCaptureResult> capture(RenderSubmission submission, engine::RenderCaptureDesc desc);
    Result<RenderSurfaceState> resize(int width, int height);
    void enableIBL(std::string hdrPath);
    ResultVoid clearAssetResources();

    /// owner 线程非阻塞回收资源 ACK / worker 失败事件。
    std::vector<RenderWorkerEvent> drainEvents();
    std::optional<Error> failureSnapshot() const;

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
        std::function<ResultVoid(RenderExecutor&)> execute;
        bool fatalOnFailure = false;
        uint64_t resourceSequence = 0;
        uint64_t resourceBatchId = 0;
        std::function<void()> complete;
        std::function<void(const Error&)> fail;
    };

    struct PendingFrame {
        RenderSubmission submission;
        uint64_t requiredResourceSequence = 0;
    };

    using Initializer = std::function<ResultVoid(RenderExecutor&)>;

    ResultVoid start(Initializer initialize);
    void run(std::stop_token stopToken, Initializer initialize, std::promise<ResultVoid> ready);
    bool enqueue(ControlTask task);
    Result<uint64_t> enqueueSubmissionResourcesLocked(RenderSubmission& submission);
    bool hasExecutableFrameLocked() const;
    ResultVoid executeLatest(RenderExecutor& executor);
    void publishSurfaceState(const RenderExecutor& executor);
    void failWorker(const Error& error, uint64_t resourceSequence = 0, uint64_t resourceBatchId = 0);

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<ControlTask> controls_;
    std::optional<PendingFrame> latest_frame_;
    RenderWorkerProtocol protocol_;
    std::jthread worker_;
    std::atomic<Lifecycle> lifecycle_ = Lifecycle::Stopped;
    RenderSurfaceState surface_state_;
};

}  // namespace mulan::view::detail
