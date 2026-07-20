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

#include "capture_target.h"
#include "present_surface.h"
#include "../render_surface_state.h"
#include "render_device_context.h"

#include "../../frontend/render_frame_submission.h"

#include <mulan/core/result/error.h>
#include "../../backend/forward_renderer.h"
#include "../../frontend/render_capture.h"

#include <optional>
#include <string>

namespace mulan::engine::detail {

class RenderExecutor {
public:
    explicit RenderExecutor(RenderDeviceContext& context);
    ~RenderExecutor();

    RenderExecutor(const RenderExecutor&) = delete;
    RenderExecutor& operator=(const RenderExecutor&) = delete;

    ResultVoid init(const RenderSurfaceConfig& config, int width, int height);
    void shutdown();

    bool isInitialized() const;
    RenderSurfaceState presentSurfaceState() const;

    ResultVoid prepareResources(const engine::RenderResourcePrepareList& prepare);
    ResultVoid executeFrame(const RenderFrameSubmission& submission);
    Result<engine::RenderCaptureResult> capture(const RenderFrameSubmission& submission,
                                                const engine::RenderCaptureDesc& desc);
    Result<RenderSurfaceState> resize(int width, int height);
    void enableIBL(const std::string& hdrPath);
    ResultVoid clearAssetResources();

private:
    ResultVoid initRenderer();
    ResultVoid configureCaptureTarget(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    RenderSurfaceState makeRenderSurfaceState() const;
    void shutdownResources();

    // 非拥有引用；RenderThread 保证先销毁全部 Executor，再销毁设备上下文。
    RenderDeviceContext& device_context_;
    PresentSurface present_surface_;
    std::optional<CaptureTarget> capture_target_;
    engine::ForwardRenderer forward_renderer_;
    engine::DeviceResourceClientId resource_client_ = 0;
    bool initialized_ = false;
};

}  // namespace mulan::engine::detail
