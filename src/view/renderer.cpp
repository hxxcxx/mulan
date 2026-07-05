/**
 * @file renderer.cpp
 * @brief Renderer 实现
 * @date 2026-07-03
 */

#include "renderer.h"
#include "render_surface.h"

#include "mulan/engine/rhi/device.h"
#include "mulan/engine/rhi/render_target.h"
#include "mulan/engine/rhi/render_types.h"
#include "mulan/engine/rhi/swap_chain.h"
#include "mulan/engine/render/frame/render_frame.h"
#include "mulan/engine/render/material/material_cache.h"
#include "mulan/engine/render/overlay/view_cube_stage.h"
#include "mulan/engine/render/texture_cache.h"
#include "mulan/engine/render/environment_map.h"

#include <cstdio>
#include <span>

namespace mulan::view {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    // 资源由 shutdown() 显式释放。
}

bool Renderer::init(engine::RHIDevice& device,
                    engine::LightEnvironment& lightEnv,
                    engine::TextureFormat colorFmt,
                    engine::TextureFormat depthFmt) {
    if (initialized_) return true;

    // 纹理缓存 + 材质缓存（去单例化，由 Renderer 持有）
    texture_cache_  = std::make_unique<engine::TextureCache>(&device);
    material_cache_ = std::make_unique<engine::MaterialCache>();

    resources_ = std::make_unique<engine::RenderResourceCache>(device);

    auto& matCache = *material_cache_;

    engine::RenderTargetInfo targetInfo;
    targetInfo.colorFormat = colorFmt;
    targetInfo.depthFormat = depthFmt;
    targetInfo.hasDepth = true;

    face_stage_ = std::make_unique<engine::FaceStage>(device, *resources_, matCache, lightEnv);
    if (!face_stage_->init(device, targetInfo))
        return false;

    // IBL 烘焙不在 init 发生：按需由 enableIBL() 触发，
    // 让调用方（DocumentSession）根据模型类型决定是否启用。

    edge_stage_ = std::make_unique<engine::EdgeStage>(device, *resources_, matCache, lightEnv);
    if (!edge_stage_->init(device, targetInfo))
        return false;

    view_cube_stage_ = std::make_unique<engine::ViewCubeStage>(device);
    if (!view_cube_stage_->init(device, targetInfo)) {
        std::fprintf(stderr, "[Renderer] ViewCube init failed (non-fatal)\n");
        view_cube_stage_.reset();
    }

    initialized_ = true;
    return true;
}

void Renderer::shutdown(engine::RHIDevice& device) {
    if (!initialized_) return;
    device.waitIdle();
    view_cube_stage_.reset();
    edge_stage_.reset();
    face_stage_.reset();
    resources_.reset();
    material_cache_.reset();
    texture_cache_.reset();
    ibl_.reset();
    initialized_ = false;
}

void Renderer::setScene(const render_scene::RenderScene* scene,
                        const asset::AssetLibrary* assets) {
    builder_.setScene(scene, assets);
}

void Renderer::enableIBL(engine::RHIDevice& device, const std::string& hdrPath) {
    // 幂等：已烘焙过直接返回（同一个 Renderer 实例生命周期内只烘一次）
    if (ibl_) return;

    // HDR 文件存在性检查：缺失则静默降级，给一行清晰提示
    FILE* test = nullptr;
#ifdef _WIN32
    fopen_s(&test, hdrPath.c_str(), "rb");
#else
    test = fopen(hdrPath.c_str(), "rb");
#endif
    if (!test) {
        std::fprintf(stderr, "[Renderer] IBL requested but HDR not found: %s "
                             "(place a .hdr file there)\n", hdrPath.c_str());
        return;
    }
    fclose(test);

    ibl_ = std::make_unique<engine::IBLPipeline>();
    if (ibl_->bake(device, hdrPath)) {
        if (face_stage_) {
            face_stage_->setIBLTextures(ibl_->irradiance(), ibl_->prefilter(), ibl_->brdfLUT());
        }
    } else {
        ibl_.reset();  // 烘焙失败，回归 fallback
    }
}

void Renderer::render(engine::RHIDevice& device,
                      RenderSurface& surface,
                      const ViewState& viewState) {
    if (!initialized_) return;

    if (resources_) {
        device.beginUploadBatch();
        builder_.rebuild(*resources_,
                         face_stage_ ? face_stage_->pipelineState() : nullptr,
                         edge_stage_ ? edge_stage_->pipelineState() : nullptr,
                         *texture_cache_,
                         *material_cache_);
        device.flushUploadBatch();
    }

    const std::span<const engine::MeshDrawCommand> emptyCommands;
    if (face_stage_)
        face_stage_->setDrawCommands(viewState.showFaces ? builder_.solidCommands() : emptyCommands);
    if (edge_stage_)
        edge_stage_->setDrawCommands(viewState.showEdges ? builder_.wireCommands() : emptyCommands);

    auto* sc = surface.swapChain();
    auto* rt = surface.renderTarget();
    device.beginFrame(sc ? sc : nullptr);
    auto* cmd = device.frameCommandList();
    cmd->begin();

    if (rt)
        cmd->beginRenderPass(rt->renderPassBeginInfo());
    else if (sc)
        cmd->beginRenderPass(sc->renderPassBeginInfo());

    engine::Viewport vp;
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(viewState.width);
    vp.height   = static_cast<float>(viewState.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->setViewport(vp);

    engine::ScissorRect scRect;
    scRect.x      = 0;
    scRect.y      = 0;
    scRect.width  = static_cast<int32_t>(viewState.width);
    scRect.height = static_cast<int32_t>(viewState.height);
    cmd->setScissorRect(scRect);

    engine::RenderView renderView;
    renderView.viewMatrix = viewState.viewMatrix;
    renderView.projectionMatrix = viewState.projectionMatrix;
    renderView.cameraPosition = viewState.cameraPosition;
    renderView.width = static_cast<uint32_t>(viewState.width);
    renderView.height = static_cast<uint32_t>(viewState.height);
    renderView.showFaces = viewState.showFaces;
    renderView.showEdges = viewState.showEdges;
    renderView.showViewCube = viewState.showViewCube;

    engine::RenderTargetInfo frameTargetInfo;
    frameTargetInfo.width = renderView.width;
    frameTargetInfo.height = renderView.height;
    frameTargetInfo.hasDepth = true;
    frameTargetInfo.presentable = sc != nullptr;

    engine::RenderFrame frame{device, *cmd, renderView, frameTargetInfo};
    if (face_stage_) face_stage_->execute(frame);
    if (edge_stage_) edge_stage_->execute(frame);
    // 两个 pass 各持有独立的 material UBO，uploadDirtyMaterials 不再自行清空脏集合，
    // 故在此处（所有 pass 都上传完毕后）统一清空，避免下帧重复全量上传。
    if (face_stage_ || edge_stage_)
        material_cache_->clearDirtyMaterials();

    if (view_cube_stage_) {
        view_cube_stage_->setPipelines(face_stage_ ? face_stage_->pipelineState() : nullptr,
                                       edge_stage_ ? edge_stage_->pipelineState() : nullptr);
        view_cube_stage_->setFallbackResources(face_stage_ ? face_stage_->defaultWhiteTexture() : nullptr,
                                               face_stage_ ? face_stage_->defaultSampler() : nullptr);
        view_cube_stage_->execute(frame);
    }

    cmd->endRenderPass();
    cmd->end();

    if (rt)
        device.submitOffscreen();
    else
        device.submitAndPresent(sc);
}

} // namespace mulan::view
