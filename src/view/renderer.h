/**
 * @file renderer.h
 * @brief Renderer —— 一帧渲染执行入口
 * @date 2026-07-03
 *
 * 从 ViewContext 抽出的渲染执行层。持有 RenderGraph、DrawCommandBuilder
 * 和 RenderResourceCache（现 RenderResourceCache）。把 RenderScene + camera
 * 转换为 render pass 可消费的 commands，调用 RHI begin frame、execute graph、present。
 *
 * 不处理 Qt 事件，不修改 Document，不持有 UI widget。
 * camera/lightEnv 由 ViewContext 拥有，init 时绑定引用（阶段 4 将由 ViewState 取代）。
 */

#pragma once

#include "draw_command_builder.h"
#include "view_state.h"

#include "mulan/engine/render/render_resource_cache.h"
#include "mulan/engine/render/graph/render_graph.h"
#include "mulan/engine/render/light_environment.h"

#include <memory>

namespace mulan::engine {
class RHIDevice;
class PipelineState;
class ViewCubeRenderer;
} // namespace mulan::engine

namespace mulan::render_scene {
class RenderScene;
}

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {

class RenderSurface;

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// 初始化渲染图与资源缓存。lightEnv 引用在 ViewContext 生命周期内稳定。
    bool init(engine::RHIDevice& device,
              engine::LightEnvironment& lightEnv,
              engine::TextureFormat colorFmt,
              engine::TextureFormat depthFmt);

    void shutdown(engine::RHIDevice& device);

    void setScene(const render_scene::RenderScene* scene,
                  const asset::AssetLibrary* assets);

    /// 执行一帧渲染。viewState 是当帧只读视图快照（相机矩阵等）。
    void render(engine::RHIDevice& device,
                RenderSurface& surface,
                const ViewState& viewState);

    engine::RenderResourceCache& resources() { return *gpu_; }
    engine::RenderGraph& renderGraph() { return render_graph_; }

    bool isInitialized() const { return initialized_; }

private:
    bool initViewCube(engine::RHIDevice* device,
                      engine::TextureFormat colorFmt,
                      engine::TextureFormat depthFmt);

    std::unique_ptr<engine::RenderResourceCache> gpu_storage_;
    engine::RenderResourceCache* gpu_ = nullptr;

    DrawCommandBuilder builder_;
    engine::RenderGraph render_graph_;
    std::unique_ptr<engine::ViewCubeRenderer> view_cube_renderer_;

    bool initialized_ = false;
};

} // namespace mulan::view
