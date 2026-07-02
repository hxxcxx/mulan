/**
 * @file view_runtime.h
 * @brief ViewRuntime 是消费 RenderScene 的视图运行时
 *
 * 持有 RHIDevice、SwapChain/RenderTarget、Camera、GpuResourceManager、
 * RenderGraph 和交互 Operator。
 */

#pragma once

#include "view_config.h"
#include "view_renderer.h"

#include "mulan/engine/interaction/camera_manipulator.h"
#include "mulan/engine/interaction/input_event.h"
#include "mulan/engine/interaction/operator.h"
#include "mulan/engine/render/gpu_resource_manager.h"
#include "mulan/engine/render/graph/render_graph.h"
#include "mulan/engine/render/light_environment.h"
#include "mulan/engine/render/viewcube/view_cube_renderer.h"
#include "mulan/engine/rhi/buffer.h"
#include "mulan/engine/rhi/device.h"
#include "mulan/engine/rhi/render_target.h"
#include "mulan/engine/rhi/swap_chain.h"
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

class ViewRuntime {
public:
    ViewRuntime();
    ~ViewRuntime();

    ViewRuntime(const ViewRuntime&) = delete;
    ViewRuntime& operator=(const ViewRuntime&) = delete;

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

    engine::GpuResourceManager& gpu() { return *gpu_; }
    const engine::GpuResourceManager& gpu() const { return *gpu_; }

    engine::RenderGraph& renderGraph() { return render_graph_; }

    engine::LightEnvironment& lightEnvironment() { return light_env_; }
    const engine::LightEnvironment& lightEnvironment() const { return light_env_; }

    engine::RenderTarget* renderTarget() const { return render_target_.get(); }

private:
    bool initRendering(int width, int height);
    bool initSceneRenderer();
    void cleanup();

    std::shared_ptr<engine::RHIDevice> device_;

    std::unique_ptr<engine::SwapChain> swapchain_;
    std::unique_ptr<engine::RenderTarget> render_target_;
    std::unique_ptr<engine::Buffer> staging_buffer_;

    const render_scene::RenderScene* render_scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;
    ViewRenderer renderer_;

    std::unique_ptr<engine::GpuResourceManager> gpu_storage_;
    engine::GpuResourceManager* gpu_ = nullptr;

    engine::Camera camera_{engine::CameraMode::Trackball};

    std::unique_ptr<engine::Operator> default_op_;
    std::vector<std::unique_ptr<engine::Operator>> op_stack_;
    std::unique_ptr<engine::ViewCubeRenderer> view_cube_renderer_;

    engine::RenderGraph render_graph_;
    engine::LightEnvironment light_env_;

    int width_ = 800;
    int height_ = 600;
    bool initialized_ = false;
};

} // namespace mulan::view
