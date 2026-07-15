/**
 * @file render_worker.h
 * @brief RenderWorker 是 RenderSession 接入设备级 GpuExecutionDomain 的客户端外观
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include "gpu_execution_domain.h"

namespace mulan::view::detail {

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

    std::vector<RenderWorkerEvent> drainEvents();
    std::optional<Error> failureSnapshot() const;
    RenderSurfaceState surfaceState() const;

    /// Debug/测试统计：同一 domainId 表示多个 Worker 实际共享一条 GPU 线程。
    GpuExecutionDomainStats domainStats() const;

private:
    ResultVoid attach(const ViewConfig& config, int width, int height, bool offscreen);

    std::shared_ptr<GpuExecutionDomain> domain_;
    GpuExecutionClientId client_ = 0;
};

}  // namespace mulan::view::detail
