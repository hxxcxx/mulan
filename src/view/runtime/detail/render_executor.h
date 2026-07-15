/**
 * @file render_executor.h
 * @brief RenderExecutor 封装单个视图在线程亲和环境中的 GPU 执行状态。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 同步模式由调用线程独占，线程模式由 RenderWorker 独占。所有 RHI 操作统一遵循
 * Executor 锁到共享 Device 锁的顺序；上层只能读取不可变的表面状态快照。
 */

#pragma once

#include "render_device_context.h"
#include "render_surface.h"
#include "render_surface_state.h"

#include "scene_sync/render_submission.h"

#include <mulan/core/result/error.h>
#include <mulan/render/backend/render_renderer.h>
#include <mulan/render/frontend/render_capture.h>
#include <mulan/render/light_environment.h>

#include <memory>
#include <mutex>
#include <string>

namespace mulan::view::detail {

class RenderExecutor {
public:
    RenderExecutor() = default;
    ~RenderExecutor();

    RenderExecutor(const RenderExecutor&) = delete;
    RenderExecutor& operator=(const RenderExecutor&) = delete;

    core::Result<void> initWindow(const ViewConfig& config, int width, int height);
    core::Result<void> initOffscreen(const ViewConfig& config, int width, int height);
    void shutdown();

    bool isInitialized() const;
    RenderSurfaceState surfaceState() const;

    core::Result<void> prepareResources(const engine::RenderResourcePrepareList& prepare);
    core::Result<void> executeFrame(const RenderSubmission& submission);
    core::Result<engine::RenderCaptureResult> capture(const RenderSubmission& submission,
                                                      const engine::RenderCaptureDesc& desc);
    core::Result<RenderSurfaceState> resize(int width, int height);
    void enableIBL(const std::string& hdrPath);
    void clearAssetResources();

private:
    core::Result<void> initRenderer();
    bool configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    RenderSurfaceState surfaceStateLocked() const;
    void shutdownLocked();

    std::shared_ptr<RenderDeviceContext> device_context_;
    RenderSurface surface_;
    RenderSurface capture_surface_;
    // RenderRenderer 的资源对象持有此成员引用，因此 LightEnvironment 必须比 renderer_ 更晚析构。
    engine::LightEnvironment light_environment_;
    engine::RenderRenderer renderer_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

}  // namespace mulan::view::detail
