/**
 * @file renderer.h
 * @brief Renderer —— 一帧渲染执行入口
 * @author hxxcxx
 * @date 2026-07-03
 *
 * 从 ViewContext 抽出的视图渲染适配层。负责把 RenderScene + camera 同步为
 * engine frontend 的 RenderRequest，并交给 engine backend 执行。
 *
 * 不处理 Qt 事件，不修改 Document，不持有 UI widget。
 * lightEnv 由 ViewContext 拥有，init 时绑定引用；相机数据来自每帧 ViewState 快照。
 */

#pragma once

#include "render_world_sync.h"
#include "view_state.h"

#include "mulan/engine/render/backend/render_renderer.h"
#include "mulan/engine/render/frontend/render_world.h"
#include "mulan/engine/render/light_environment.h"

#include <string>

namespace mulan::engine {
class RHIDevice;
}  // namespace mulan::engine

namespace mulan::view {
class RenderScene;
class PreviewLayer;
}  // namespace mulan::view

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

    /// 初始化 stages 与资源缓存。lightEnv 引用在 ViewContext 生命周期内稳定。
    /// IBL 烘焙不在此处发生——按需通过 enableIBL() 触发，由调用方根据模型类型决定。
    bool init(engine::RHIDevice& device, engine::LightEnvironment& lightEnv, engine::TextureFormat colorFmt,
              engine::TextureFormat depthFmt, uint32_t sampleCount);

    void shutdown(engine::RHIDevice& device);

    void setScene(engine::RHIDevice* device, const RenderScene* scene, const asset::AssetLibrary* assets);
    void setPreviewLayer(const PreviewLayer* preview);

    /// 按需烘焙 IBL 三件套（irradiance/prefilter/BRDF LUT）。
    /// 已烘焙过则跳过（幂等）。HDR 文件不存在则静默失败。
    /// 调用时机：DocumentSession 在 attachViewContext 时按模型类型决定是否调用。
    void enableIBL(engine::RHIDevice& device, const std::string& hdrPath);

    /// 执行一帧渲染。viewState 是当帧只读视图快照（相机矩阵等）。
    void render(engine::RHIDevice& device, RenderSurface& surface, const ViewState& viewState);

    bool isInitialized() const { return initialized_; }
    const RenderWorldSyncStats& lastWorldSyncStats() const { return render_world_sync_.lastStats(); }

private:
    void syncEngineWorld();
    engine::RenderRequest buildRequest(RenderSurface& surface, const ViewState& viewState);
    engine::RenderSurfaceBinding surfaceBinding(RenderSurface& surface) const;

    const RenderScene* scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;
    const PreviewLayer* preview_ = nullptr;

    RenderWorldSync render_world_sync_;
    engine::RenderWorld render_world_;
    engine::RenderWorldSnapshot world_snapshot_;
    engine::RenderResourcePrepareList resource_prepare_;
    engine::RenderRenderer render_renderer_;

    bool world_dirty_ = true;
    uint64_t synced_preview_generation_ = 0;
    bool resource_prepare_pending_ = false;
    bool initialized_ = false;
};

}  // namespace mulan::view
