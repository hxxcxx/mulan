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
#include "mulan/engine/render/material/material_cache.h"
#include "mulan/engine/render/viewcube/view_cube_renderer.h"

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

    resources_ = std::make_unique<engine::RenderResourceCache>(device);

    auto& matCache = engine::MaterialCache::instance();

    solid_pass_ = std::make_unique<engine::GeometryPass>(
        device, *resources_, matCache, lightEnv,
        engine::GeometryPassConfig{
            "albedo", engine::PrimitiveTopology::TriangleList,
            /*depthWrite=*/true, "Forward", /*sampleTextures=*/true});
    if (!solid_pass_->init(colorFmt, depthFmt, true))
        return false;

    wire_pass_ = std::make_unique<engine::GeometryPass>(
        device, *resources_, matCache, lightEnv,
        engine::GeometryPassConfig{
            "edge", engine::PrimitiveTopology::LineList, /*depthWrite=*/false, "Edge"});
    if (!wire_pass_->init(colorFmt, depthFmt, true))
        return false;

    if (!initViewCube(&device, colorFmt, depthFmt)) {
        std::fprintf(stderr, "[Renderer] ViewCube init failed (non-fatal)\n");
    }

    initialized_ = true;
    return true;
}

void Renderer::shutdown(engine::RHIDevice& device) {
    if (!initialized_) return;
    device.waitIdle();
    view_cube_renderer_.reset();
    wire_pass_.reset();
    solid_pass_.reset();
    resources_.reset();
    initialized_ = false;
}

void Renderer::setScene(const render_scene::RenderScene* scene,
                        const asset::AssetLibrary* assets) {
    builder_.setScene(scene, assets);
}

void Renderer::render(engine::RHIDevice& device,
                      RenderSurface& surface,
                      const ViewState& viewState) {
    if (!initialized_) return;

    if (resources_)
        builder_.rebuild(*resources_,
                         solid_pass_ ? solid_pass_->pipelineState() : nullptr,
                         wire_pass_ ? wire_pass_->pipelineState() : nullptr);

    const std::span<const engine::MeshDrawCommand> emptyCommands;
    if (solid_pass_)
        solid_pass_->setDrawCommands(viewState.showFaces ? builder_.solidCommands() : emptyCommands);
    if (wire_pass_)
        wire_pass_->setDrawCommands(viewState.showEdges ? builder_.wireCommands() : emptyCommands);

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

    engine::PassContext ctx;
    ctx.cmd    = cmd;
    ctx.width  = viewState.width;
    ctx.height = viewState.height;
    ctx.camera.viewMatrix       = viewState.viewMatrix;
    ctx.camera.projectionMatrix = viewState.projectionMatrix;
    ctx.camera.eyePosition      = viewState.cameraPosition;
    if (solid_pass_) solid_pass_->execute(ctx);
    if (wire_pass_) wire_pass_->execute(ctx);
    // 两个 pass 各持有独立的 material UBO，uploadDirtyMaterials 不再自行清空脏集合，
    // 故在此处（所有 pass 都上传完毕后）统一清空，避免下帧重复全量上传。
    if (solid_pass_ || wire_pass_)
        engine::MaterialCache::instance().clearDirtyMaterials();

    if (viewState.showViewCube && view_cube_renderer_) {
        view_cube_renderer_->render(cmd, viewState.viewMatrix,
                                    static_cast<uint32_t>(viewState.width),
                                    static_cast<uint32_t>(viewState.height));
    }

    cmd->endRenderPass();
    cmd->end();

    if (rt)
        device.submitOffscreen();
    else
        device.submitAndPresent(sc);
}

bool Renderer::initViewCube(engine::RHIDevice* device,
                            engine::TextureFormat colorFmt,
                            engine::TextureFormat depthFmt) {
    view_cube_renderer_ = std::make_unique<engine::ViewCubeRenderer>(device);
    if (!view_cube_renderer_->init(colorFmt, depthFmt)) {
        view_cube_renderer_.reset();
        return false;
    }
    return true;
}

} // namespace mulan::view
