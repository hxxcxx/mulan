/**
 * @file renderer.h
 * @brief Renderer —— 一帧渲染执行入口
 * @author hxxcxx
 * @date 2026-07-03
 *
 * 从 ViewContext 抽出的视图渲染适配层。负责消费 RenderSubmission 并交给
 * engine frontend/backend 执行。
 *
 * 不处理 Qt 事件，不修改 Document，不持有 UI widget。
 * lightEnv 由 ViewContext 拥有，init 时绑定引用；相机数据来自每帧 ViewState 快照。
 */

#pragma once

#include <mulan/view/scene_sync/render_submission.h>

#include "mulan/render/backend/render_renderer.h"
#include "mulan/render/frontend/render_world.h"
#include "mulan/render/light_environment.h"

#include <string>

namespace mulan::engine {
class RHIDevice;
}  // namespace mulan::engine

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

    /// 清空当前文档派生的 GPU 缓存；文档资产源发生切换时调用。
    void clearAssetResources(engine::RHIDevice& device);

    /// 按需烘焙 IBL 三件套（irradiance/prefilter/BRDF LUT）。
    /// 已烘焙过则跳过（幂等）。HDR 文件不存在则静默失败。
    /// 调用时机：DocumentSession 在 attachViewContext 时按模型类型决定是否调用。
    void enableIBL(engine::RHIDevice& device, const std::string& hdrPath);

    /// 执行一帧渲染。提交对象不引用 editor / view 的活对象。
    void render(engine::RHIDevice& device, RenderSurface& surface, const RenderSubmission& submission);

    bool isInitialized() const { return initialized_; }
    const engine::RenderWorkloadStats& lastRenderWorkloadStats() const { return render_renderer_.lastWorkloadStats(); }
    const engine::RenderCompilerStats& lastRenderCompilerStats() const { return render_renderer_.lastCompilerStats(); }

private:
    engine::RenderRequest buildRequest(RenderSurface& surface, const RenderSubmission& submission);
    engine::RenderSurfaceBinding surfaceBinding(RenderSurface& surface) const;

    engine::RenderRenderer render_renderer_;

    bool initialized_ = false;
};

}  // namespace mulan::view
