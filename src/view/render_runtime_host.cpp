#include "render_runtime_host.h"

namespace mulan::view {

RenderRuntimeHost::~RenderRuntimeHost() {
    shutdown();
}

core::Result<void> RenderRuntimeHost::initWindow(const ViewConfig& config, int width, int height,
                                                 engine::LightEnvironment& lightEnv) {
    return runtime_.initWindow(config, width, height, lightEnv);
}

core::Result<void> RenderRuntimeHost::initOffscreen(int width, int height, engine::LightEnvironment& lightEnv) {
    return runtime_.initOffscreen(width, height, lightEnv);
}

void RenderRuntimeHost::shutdown() {
    runtime_.shutdown();
}

bool RenderRuntimeHost::isInitialized() const {
    return runtime_.isInitialized();
}

void RenderRuntimeHost::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    runtime_.setRenderScene(scene, assets);
}

void RenderRuntimeHost::setPreviewLayer(const PreviewLayer* preview) {
    runtime_.setPreviewLayer(preview);
}

void RenderRuntimeHost::render(const ViewState& viewState) {
    runtime_.render(viewState);
}

void RenderRuntimeHost::resize(int width, int height) {
    runtime_.resize(width, height);
}

void RenderRuntimeHost::enableIBL(const std::string& hdrPath) {
    runtime_.enableIBL(hdrPath);
}

bool RenderRuntimeHost::isOffscreenSurface() const {
    return runtime_.surface().isOffscreen();
}

uint32_t RenderRuntimeHost::surfaceWidth() const {
    return static_cast<uint32_t>(runtime_.surface().width());
}

uint32_t RenderRuntimeHost::surfaceHeight() const {
    return static_cast<uint32_t>(runtime_.surface().height());
}

bool RenderRuntimeHost::readbackPixels(std::vector<uint8_t>& pixels) {
    return runtime_.readbackPixels(pixels);
}

bool RenderRuntimeHost::configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width,
                                                uint32_t height) {
    return runtime_.configureCaptureSurface(desc, width, height);
}

bool RenderRuntimeHost::configureOffscreenSurface(const RenderSurfaceDesc& desc) {
    return runtime_.configureOffscreenSurface(desc);
}

std::optional<RenderSurfaceDesc> RenderRuntimeHost::offscreenSurfaceDesc() const {
    return runtime_.offscreenSurfaceDesc();
}

const RenderWorldSyncStats& RenderRuntimeHost::lastWorldSyncStats() const {
    return runtime_.lastWorldSyncStats();
}

}  // namespace mulan::view
