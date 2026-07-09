/**
 * @file renderer.cpp
 * @brief Renderer 实现
 * @date 2026-07-03
 */

#include "renderer.h"
#include "preview_layer.h"
#include "render_surface.h"

#include "mulan/engine/rhi/device.h"

namespace mulan::view {
namespace {

engine::DisplayMode toDisplayMode(RenderMode mode) {
    switch (mode) {
    case RenderMode::Shaded: return engine::DisplayMode::Shaded;
    case RenderMode::ShadedWithEdges: return engine::DisplayMode::ShadedWithEdges;
    case RenderMode::Wireframe: return engine::DisplayMode::Wireframe;
    }
    return engine::DisplayMode::ShadedWithEdges;
}

engine::SurfaceTechnique toSurfaceTechnique(SurfaceShading shading) {
    switch (shading) {
    case SurfaceShading::SolidLit: return engine::SurfaceTechnique::SolidLit;
    case SurfaceShading::SurfacePBR: return engine::SurfaceTechnique::SurfacePBR;
    }
    return engine::SurfaceTechnique::SolidLit;
}

}  // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() {
    // 资源由 shutdown() 显式释放。
}

bool Renderer::init(engine::RHIDevice& device, engine::LightEnvironment& lightEnv, engine::TextureFormat colorFmt,
                    engine::TextureFormat depthFmt, uint32_t sampleCount) {
    if (initialized_)
        return true;

    if (!render_renderer_.init(device, lightEnv, colorFmt, depthFmt, sampleCount))
        return false;

    initialized_ = true;
    return true;
}

void Renderer::shutdown(engine::RHIDevice& device) {
    if (!initialized_)
        return;
    render_renderer_.shutdown(device);
    initialized_ = false;
}

void Renderer::setScene(engine::RHIDevice* device, const RenderScene* scene, const asset::AssetLibrary* assets) {
    // 文档切换：assets 指针变化时，释放旧文档派生的 GPU 资源（几何 buffer 等）。
    // 资产身份 key 与旧文档绑定，新文档的资产身份不同，必须清空避免悬垂 + 释放显存。
    if (assets_ != assets && initialized_ && device) {
        render_renderer_.clearAssetResources(*device);
    }
    scene_ = scene;
    assets_ = assets;
    world_dirty_ = true;
}

void Renderer::setPreviewLayer(const PreviewLayer* preview) {
    if (preview_ == preview) {
        return;
    }

    preview_ = preview;
    synced_preview_generation_ = 0;
    world_dirty_ = true;
}

void Renderer::enableIBL(engine::RHIDevice& device, const std::string& hdrPath) {
    render_renderer_.enableIBL(device, hdrPath);
}

void Renderer::render(engine::RHIDevice& device, RenderSurface& surface, const ViewState& viewState) {
    if (!initialized_)
        return;

    syncEngineWorld();

    auto request = buildRequest(surface, viewState);
    render_renderer_.render(device, surfaceBinding(surface), request);
    resource_prepare_pending_ = false;
}

engine::RenderRequest Renderer::buildRequest(RenderSurface& surface, const ViewState& viewState) {
    engine::RenderRequest request;
    request.world = (scene_ && assets_) ? &world_snapshot_ : nullptr;
    request.prepare = (scene_ && assets_ && resource_prepare_pending_) ? &resource_prepare_ : nullptr;
    request.view.viewMatrix = viewState.viewMatrix;
    request.view.projectionMatrix = viewState.projectionMatrix;
    request.view.cameraPosition = viewState.cameraPosition;
    request.view.width = static_cast<uint32_t>(viewState.width);
    request.view.height = static_cast<uint32_t>(viewState.height);
    request.output.mode = surface.isOffscreen() ? engine::RenderTargetMode::Capture : engine::RenderTargetMode::Present;
    request.output.width = request.view.width;
    request.output.height = request.view.height;
    request.output.readback = surface.isOffscreen();
    request.output.capture.width = request.output.width;
    request.output.capture.height = request.output.height;
    request.output.capture.format =
            surface.renderTarget() ? surface.renderTarget()->colorFormat() : surface.swapChain()->colorFormat();
    request.output.capture.depthFormat =
            surface.renderTarget() ? surface.renderTarget()->depthFormat() : surface.swapChain()->depthFormat();
    request.output.capture.readback = request.output.readback;
    request.options.displayMode = toDisplayMode(viewState.renderMode);
    request.options.surfaceTechnique = toSurfaceTechnique(viewState.surfaceShading);
    request.options.hoveredPickId = viewState.hoveredPickId;
    request.options.selectionVisuals = viewState.selectionVisuals;
    request.options.showSurfaces = viewState.showFaces;
    request.options.showEdges = viewState.showEdges;
    request.options.showOverlays = viewState.showOverlays;
    request.options.showViewCube = viewState.showViewCube;
    request.options.viewCubeLayout = viewState.viewCubeLayout;
    request.options.viewCubeInteraction = viewState.viewCubeInteraction;
    return request;
}

void Renderer::syncEngineWorld() {
    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
    const bool previewDirty = previewGeneration != synced_preview_generation_;
    const bool previewOnlyDirty = previewDirty && !world_dirty_;
    if (previewDirty) {
        world_dirty_ = true;
    }

    if (!world_dirty_) {
        return;
    }

    if (!scene_ || !assets_) {
        render_world_.clear();
        world_snapshot_ = {};
        resource_prepare_.clear();
        render_world_sync_.clearStats();
        resource_prepare_pending_ = false;
        world_dirty_ = false;
        return;
    }

    render_world_sync_.rebuild(*scene_, *assets_, preview_, render_world_, &resource_prepare_, !previewOnlyDirty);
    world_snapshot_ = render_world_.snapshot();
    resource_prepare_pending_ = true;
    world_dirty_ = false;
    synced_preview_generation_ = previewGeneration;
}

engine::RenderSurfaceBinding Renderer::surfaceBinding(RenderSurface& surface) const {
    return engine::RenderSurfaceBinding{
        .swapChain = surface.swapChain(),
        .renderTarget = surface.renderTarget(),
    };
}

}  // namespace mulan::view
