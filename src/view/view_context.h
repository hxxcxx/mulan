/**
 * @file view_context.h
 * @brief ViewContext 管理视图状态、相机和交互，并生成 ViewState。
 */
#pragma once

#include "render_surface.h"
#include "renderer.h"
#include "view_config.h"
#include "view_state.h"

#include "mulan/engine/interaction/camera_manipulator.h"
#include "mulan/engine/interaction/input_event.h"
#include "mulan/engine/interaction/operator.h"
#include "mulan/engine/render/light_environment.h"
#include "mulan/engine/rhi/device.h"
#include "mulan/engine/render/camera/camera.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::render_scene {
class RenderScene;
}

namespace mulan::view {

class ViewContext {
public:
    ViewContext();
    ~ViewContext();

    ViewContext(const ViewContext&) = delete;
    ViewContext& operator=(const ViewContext&) = delete;

    bool init(const ViewConfig& config, int width, int height);
    bool initOffscreen(int width, int height);
    void shutdown();

    bool isInitialized() const { return initialized_; }

    void setRenderScene(const render_scene::RenderScene* scene,
                        const asset::AssetLibrary* assets);

    void renderFrame();
    void onFrameEnd();
    void resize(int width, int height);

    void handleInput(const engine::InputEvent& event);

    void pushOperator(std::unique_ptr<engine::Operator> op);
    void popOperator();

    engine::Operator* activeOperator() const;
    engine::Operator* defaultOperator() const { return default_op_.get(); }

    bool readbackPixels(std::vector<uint8_t>& pixels);

    engine::Camera& camera() { return camera_; }
    const engine::Camera& camera() const { return camera_; }

    engine::LightEnvironment& lightEnvironment() { return light_env_; }
    const engine::LightEnvironment& lightEnvironment() const { return light_env_; }

    RenderMode renderMode() const { return render_mode_; }
    void setRenderMode(RenderMode mode) { render_mode_ = mode; }

    bool showViewCube() const { return show_view_cube_; }
    void setShowViewCube(bool show) { show_view_cube_ = show; }

    RenderSurface& surface() { return surface_; }
    const RenderSurface& surface() const { return surface_; }

    Renderer& renderer() { return renderer_; }

private:
    bool initRendering();
    ViewState buildViewState() const;
    void cleanup();

    std::shared_ptr<engine::RHIDevice> device_;
    RenderSurface surface_;
    Renderer renderer_;

    const render_scene::RenderScene* render_scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;

    engine::Camera camera_{engine::CameraMode::Trackball};

    std::unique_ptr<engine::Operator> default_op_;
    std::vector<std::unique_ptr<engine::Operator>> op_stack_;

    engine::LightEnvironment light_env_;
    RenderMode render_mode_ = RenderMode::ShadedWithEdges;
    bool show_view_cube_ = true;

    int width_ = 800;
    int height_ = 600;
    bool initialized_ = false;

    // IBL 配置（由 ViewConfig::iblEnabled / hdrPath 填充，initRendering 时传给 Renderer）
    bool ibl_enabled_ = false;
    std::string hdr_path_ = "assets/envmap.hdr";
};

} // namespace mulan::view
