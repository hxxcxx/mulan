/**
 * @file renderer.h
 * @brief Renderer —— 一帧渲染执行入口
 * @date 2026-07-03
 *
 * 从 ViewContext 抽出的渲染执行层。持有 Forward stages（实体面 / 边线）、
 * DrawCommandBuilder 和 RenderResourceCache。把 RenderScene + camera
 * 转换为 render pass 可消费的 commands，调用 RHI begin frame、execute passes、present。
 *
 * 不处理 Qt 事件，不修改 Document，不持有 UI widget。
 * camera/lightEnv 由 ViewContext 拥有，init 时绑定引用（阶段 4 将由 ViewState 取代）。
 */

#pragma once

#include "draw_command_builder.h"
#include "view_state.h"

#include "mulan/engine/render/render_resource_cache.h"
#include "mulan/engine/render/forward/edge_stage.h"
#include "mulan/engine/render/forward/face_stage.h"
#include "mulan/engine/render/light_environment.h"
#include "mulan/engine/render/texture_cache.h"
#include "mulan/engine/render/material/material_cache.h"
#include "mulan/engine/render/environment_map.h"

#include <memory>
#include <string>

namespace mulan::engine {
class RHIDevice;
class PipelineState;
class ViewCubeStage;
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

    /// 初始化 passes 与资源缓存。lightEnv 引用在 ViewContext 生命周期内稳定。
    /// IBL 烘焙不在此处发生——按需通过 enableIBL() 触发，由调用方根据模型类型决定。
    bool init(engine::RHIDevice& device,
              engine::LightEnvironment& lightEnv,
              engine::TextureFormat colorFmt,
              engine::TextureFormat depthFmt);

    void shutdown(engine::RHIDevice& device);

    void setScene(const render_scene::RenderScene* scene,
                  const asset::AssetLibrary* assets);

    /// 按需烘焙 IBL 三件套（irradiance/prefilter/BRDF LUT）。
    /// 已烘焙过则跳过（幂等）。HDR 文件不存在则静默失败。
    /// 调用时机：DocumentSession 在 attachViewContext 时按模型类型决定是否调用。
    void enableIBL(engine::RHIDevice& device, const std::string& hdrPath);

    /// 执行一帧渲染。viewState 是当帧只读视图快照（相机矩阵等）。
    void render(engine::RHIDevice& device,
                RenderSurface& surface,
                const ViewState& viewState);

    engine::RenderResourceCache& resources() { return *resources_; }

    bool isInitialized() const { return initialized_; }

private:
    // cache 在最前声明（C++ 按声明逆序析构 → cache 最后析构，
    // 此时 passes/resources 已释放，但 device 仍活，GPU 纹理可安全销毁）
    std::unique_ptr<engine::TextureCache>  texture_cache_;
    std::unique_ptr<engine::MaterialCache> material_cache_;
    std::unique_ptr<engine::IBLPipeline> ibl_;

    std::unique_ptr<engine::RenderResourceCache> resources_;

    DrawCommandBuilder builder_;
    std::unique_ptr<engine::FaceStage> face_stage_;
    std::unique_ptr<engine::EdgeStage> edge_stage_;
    std::unique_ptr<engine::ViewCubeStage> view_cube_stage_;

    bool initialized_ = false;
};

} // namespace mulan::view
