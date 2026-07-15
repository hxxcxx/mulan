/**
 * @file render_runtime.h
 * @brief RenderRuntime 管理单个视图的表面与渲染器；Device 由共享的 RenderDeviceContext 管理。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <mulan/view/scene_sync/render_surface.h>
#include <mulan/view/runtime/render_device_context.h>
#include <mulan/view/scene_sync/renderer.h>
#include <mulan/view/core/view_config.h>
#include <mulan/view/core/view_state.h>

#include <mulan/core/result/error.h>
#include <mulan/render/frontend/render_capture.h>
#include <mulan/render/light_environment.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string>

namespace mulan::view {

class RenderRuntime {
public:
    RenderRuntime() = default;
    ~RenderRuntime();

    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;

    core::Result<void> initWindow(const ViewConfig& config, int width, int height);

    core::Result<void> initOffscreen(const ViewConfig& config, int width, int height);
    core::Result<void> initOffscreen(int width, int height);

    void shutdown();

    bool isInitialized() const { return initialized_; }

    void render(const RenderSubmission& submission);
    core::Result<engine::RenderCaptureResult> capture(const RenderSubmission& submission,
                                                      const engine::RenderCaptureDesc& desc);
    void resize(int width, int height);
    void enableIBL(const std::string& hdrPath);

    void clearAssetResources();

    RenderSurface& surface() { return surface_; }
    const RenderSurface& surface() const { return surface_; }
    const engine::RenderWorkloadStats& lastRenderWorkloadStats() const { return renderer_.lastRenderWorkloadStats(); }
    const engine::RenderCompilerStats& lastRenderCompilerStats() const { return renderer_.lastRenderCompilerStats(); }

private:
    core::Result<void> initRendering();
    bool configureDedicatedCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    void shutdownNow();

    std::shared_ptr<RenderDeviceContext> device_context_;
    RenderSurface surface_;
    /// 截图不会修改或替换窗口交换链，而是在同一 Device 的同级离屏表面上渲染。
    RenderSurface capture_surface_;
    Renderer renderer_;
    engine::LightEnvironment light_environment_;
    mutable std::mutex runtime_mutex_;
    bool initialized_ = false;
};

}  // namespace mulan::view
