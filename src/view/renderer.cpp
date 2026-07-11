/**
 * @file renderer.cpp
 * @brief Renderer 实现
 * @date 2026-07-03
 */

#include "renderer.h"
#include "render_surface.h"

#include "mulan/rhi/device.h"

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
    // 即使 init() 未能完成（initialized_ 仍为 false），render_renderer_ 内部也可能
    // 已分配部分 RHI 资源。必须无条件下推 shutdown 以释放它们，否则这些资源会在
    // device 析构时触发 assertNoLiveResources 断言。
    render_renderer_.shutdown(device);
    initialized_ = false;
}

void Renderer::clearAssetResources(engine::RHIDevice& device) {
    if (initialized_) {
        render_renderer_.clearAssetResources(device);
    }
}

void Renderer::enableIBL(engine::RHIDevice& device, const std::string& hdrPath) {
    render_renderer_.enableIBL(device, hdrPath);
}

void Renderer::render(engine::RHIDevice& device, RenderSurface& surface, const RenderSubmission& submission) {
    if (!initialized_)
        return;

    auto request = buildRequest(surface, submission);
    render_renderer_.render(device, surfaceBinding(surface), request);
}

engine::RenderRequest Renderer::buildRequest(RenderSurface& surface, const RenderSubmission& submission) {
    const ViewState& viewState = submission.view;
    engine::RenderRequest request;
    request.world = submission.world.get();
    request.prepare = submission.hasResourceUpdates() ? &submission.prepare : nullptr;
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

engine::RenderSurfaceBinding Renderer::surfaceBinding(RenderSurface& surface) const {
    return engine::RenderSurfaceBinding{
        .swapChain = surface.swapChain(),
        .renderTarget = surface.renderTarget(),
    };
}

}  // namespace mulan::view
