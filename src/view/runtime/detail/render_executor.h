/**
 * @file render_executor.h
 * @brief RenderExecutor 封装单个视图在线程亲和环境中的 GPU 执行状态。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * Executor 由 RenderThread 的唯一 GPU 线程独占，不提供跨线程同步；
 * 上层只能通过所属渲染通道访问它。
 */

#pragma once

#include "render_device_context.h"
#include "render_surface.h"
#include "render_surface_state.h"

#include "scene_sync/render_submission.h"

#include <mulan/core/result/error.h>
#include <mulan/render/backend/forward_renderer.h>
#include <mulan/render/frontend/render_capture.h>

#include <string>

namespace mulan::view::detail {

class RenderExecutor {
public:
    RenderExecutor() = default;
    ~RenderExecutor();

    RenderExecutor(const RenderExecutor&) = delete;
    RenderExecutor& operator=(const RenderExecutor&) = delete;

    ResultVoid initWindow(RenderDeviceContext& context, const ViewConfig& config, int width, int height);
    void shutdown();

    bool isInitialized() const;
    RenderSurfaceState surfaceState() const;

    ResultVoid prepareResources(const engine::RenderResourcePrepareList& prepare);
    ResultVoid executeFrame(const RenderSubmission& submission);
    Result<engine::RenderCaptureResult> capture(const RenderSubmission& submission,
                                                const engine::RenderCaptureDesc& desc);
    Result<RenderSurfaceState> resize(int width, int height);
    void enableIBL(const std::string& hdrPath);
    ResultVoid clearAssetResources();

private:
    ResultVoid initRenderer();
    ResultVoid configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    RenderSurfaceState surfaceStateLocked() const;
    void shutdownLocked();

    // 非拥有借用；RenderThread 保证先关闭全部 Executor，再销毁设备上下文。
    RenderDeviceContext* device_context_ = nullptr;
    RenderSurface surface_;
    RenderSurface capture_surface_;
    engine::ForwardRenderer forward_renderer_;
    engine::DeviceResourceClientId resource_client_ = 0;
    bool initialized_ = false;
};

}  // namespace mulan::view::detail
