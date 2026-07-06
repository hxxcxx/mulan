/**
 * @file renderer.cpp
 * @brief Renderer 实现
 * @date 2026-07-03
 */

#include "renderer.h"
#include "render_surface.h"

#include "mulan/engine/rhi/device.h"

namespace mulan::view {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    // 资源由 shutdown() 显式释放。
}

bool Renderer::init(engine::RHIDevice& device, engine::LightEnvironment& lightEnv, engine::TextureFormat colorFmt,
                    engine::TextureFormat depthFmt) {
    if (initialized_)
        return true;

    if (!render_renderer_.init(device, lightEnv, colorFmt, depthFmt))
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
}

void Renderer::enableIBL(engine::RHIDevice& device, const std::string& hdrPath) {
    render_renderer_.enableIBL(device, hdrPath);
}

void Renderer::render(engine::RHIDevice& device, RenderSurface& surface, const ViewState& viewState) {
    if (!initialized_)
        return;

    if (scene_ && assets_) {
        render_world_sync_.rebuild(*scene_, *assets_, render_world_);
        world_snapshot_ = render_world_.snapshot();
    }

    auto request = buildRequest(surface, viewState);
    render_renderer_.render(device, surfaceBinding(surface), request);
}

engine::RenderRequest Renderer::buildRequest(RenderSurface& surface, const ViewState& viewState) {
    engine::RenderRequest request;
    request.world = (scene_ && assets_) ? &world_snapshot_ : nullptr;
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
    request.options.showSurfaces = viewState.showFaces;
    request.options.showEdges = viewState.showEdges;
    request.options.showOverlays = viewState.showOverlays;
    request.options.showViewCube = viewState.showViewCube;
    return request;
}

engine::RenderSurfaceBinding Renderer::surfaceBinding(RenderSurface& surface) const {
    return engine::RenderSurfaceBinding{
        .swapChain = surface.swapChain(),
        .renderTarget = surface.renderTarget(),
    };
}

}  // namespace mulan::view
