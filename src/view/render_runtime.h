/**
 * @file render_runtime.h
 * @brief RenderRuntime 拥有 view 层渲染运行时资源：device、surface 和 renderer。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_surface.h"
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

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::engine {
class RHIDevice;
}

namespace mulan::view {
class RenderScene;
}

namespace mulan::view {

class RenderRuntime {
public:
    RenderRuntime() = default;
    ~RenderRuntime();

    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;

    std::expected<void, core::Error>
    initWindow(const ViewConfig& config,
               int width,
               int height,
               engine::LightEnvironment& lightEnv);

    std::expected<void, core::Error>
    initOffscreen(int width,
                  int height,
                  engine::LightEnvironment& lightEnv);

    void shutdown();

    bool isInitialized() const { return initialized_; }

    void setRenderScene(const RenderScene* scene,
                        const asset::AssetLibrary* assets);

    void render(const ViewState& viewState);
    void resize(int width, int height);
    void enableIBL(const std::string& hdrPath);

    bool readbackPixels(std::vector<uint8_t>& pixels);
    bool configureCaptureSurface(const engine::RenderCaptureDesc& desc,
                                 uint32_t width,
                                 uint32_t height);
    bool configureOffscreenSurface(const RenderSurfaceDesc& desc);
    std::optional<RenderSurfaceDesc> offscreenSurfaceDesc() const;

    RenderSurface& surface() { return surface_; }
    const RenderSurface& surface() const { return surface_; }

private:
    std::expected<void, core::Error>
    initRendering(engine::LightEnvironment& lightEnv);

    std::shared_ptr<engine::RHIDevice> device_;
    RenderSurface surface_;
    Renderer renderer_;
    bool initialized_ = false;
};

} // namespace mulan::view
