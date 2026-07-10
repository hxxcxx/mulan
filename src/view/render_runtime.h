/**
 * @file render_runtime.h
 * @brief RenderRuntime 拥有 view 层渲染运行时资源：device、surface 和 renderer。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_surface.h"
#include "render_runtime_command.h"
#include "renderer.h"
#include "view_config.h"
#include "view_state.h"

#include <mulan/core/result/error.h>
#include <mulan/engine/render/frontend/render_capture.h>
#include <mulan/engine/render/light_environment.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mulan::engine {
class RHIDevice;
}

namespace mulan::view {

class RenderRuntime {
public:
    RenderRuntime() = default;
    ~RenderRuntime();

    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;

    core::Result<void> initWindow(const ViewConfig& config, int width, int height);

    core::Result<void> initOffscreen(int width, int height);

    void shutdown();

    bool isInitialized() const { return initialized_; }

    /// 同步执行生命周期命令。未来线程化时，此入口将改为投递有序命令并等待结果。
    RenderRuntimeCommandResult execute(RenderRuntimeCommand command);

    void render(const RenderSubmission& submission);
    void resize(int width, int height);
    void enableIBL(const std::string& hdrPath);

    bool readbackPixels(std::vector<uint8_t>& pixels);
    bool configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    bool configureOffscreenSurface(const RenderSurfaceDesc& desc);
    std::optional<RenderSurfaceDesc> offscreenSurfaceDesc() const;

    RenderSurface& surface() { return surface_; }
    const RenderSurface& surface() const { return surface_; }
    const engine::RenderWorkloadStats& lastRenderWorkloadStats() const { return renderer_.lastRenderWorkloadStats(); }
    const engine::RenderCompilerStats& lastRenderCompilerStats() const { return renderer_.lastRenderCompilerStats(); }

private:
    core::Result<void> initRendering();
    void shutdownNow();

    std::unique_ptr<engine::RHIDevice> device_;
    RenderSurface surface_;
    Renderer renderer_;
    engine::LightEnvironment light_environment_;
    bool initialized_ = false;
};

}  // namespace mulan::view
