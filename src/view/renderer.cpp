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
#include "mulan/engine/render/graph/forward_pass.h"
#include "mulan/engine/render/graph/edge_pass.h"
#include "mulan/engine/render/material/material_cache.h"
#include "mulan/engine/render/viewcube/view_cube_renderer.h"

#include <cstdio>

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

    gpu_storage_ = std::make_unique<engine::RenderResourceCache>(device);
    gpu_         = gpu_storage_.get();

    auto& matCache = engine::MaterialCache::instance();
    matCache.setDevice(&device);

    auto fwd = std::make_unique<engine::ForwardPass>(
        device, *gpu_, matCache, lightEnv);
    if (!fwd->init(colorFmt, depthFmt, true))
        return false;
    render_graph_.addPass(std::move(fwd));

    auto edge = std::make_unique<engine::EdgePass>(
        device, *gpu_, matCache, lightEnv);
    if (!edge->init(colorFmt, depthFmt, true))
        return false;
    render_graph_.addPass(std::move(edge));

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
    render_graph_.clear();
    gpu_storage_.reset();
    gpu_ = nullptr;
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

    auto* fwd  = render_graph_.pass<engine::ForwardPass>(0);
    auto* edge = render_graph_.pass<engine::EdgePass>(1);

    if (gpu_)
        builder_.rebuild(*gpu_,
                         fwd ? fwd->pipelineState() : nullptr,
                         edge ? edge->pipelineState() : nullptr);

    if (fwd)  fwd->setDrawCommands(builder_.faceCommands());
    if (edge) edge->setDrawCommands(builder_.edgeCommands());

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
    render_graph_.execute(ctx);

    if (view_cube_renderer_) {
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
