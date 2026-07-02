/**
 * @file view_context.h
 * @brief ViewContext 是消费 RenderScene 的视图运行时
 *
 * 持有 RHIDevice、RenderSurface、Camera、Renderer 和交互 Operator。
 * 渲染执行（RenderGraph / draw command / GPU 资源缓存）已下沉到 Renderer。
 */

#pragma once

#include "render_surface.h"
#include "renderer.h"
#include "view_config.h"

#include "mulan/engine/interaction/camera_manipulator.h"
#include "mulan/engine/interaction/input_event.h"
#include "mulan/engine/interaction/operator.h"
#include "mulan/engine/render/light_environment.h"
#include "mulan/engine/rhi/device.h"
#include "mulan/engine/scene/camera/camera.h"

#include <cstdint>
#include <memory>
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

    RenderSurface& surface() { return surface_; }
    const RenderSurface& surface() const { return surface_; }

    Renderer& renderer() { return renderer_; }

private:
    bool initRendering();
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

    int width_ = 800;
    int height_ = 600;
    bool initialized_ = false;
};

} // namespace mulan::view
