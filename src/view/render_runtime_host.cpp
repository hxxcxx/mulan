#include "render_runtime_host.h"

namespace mulan::view {

RenderRuntimeHost::~RenderRuntimeHost() {
    shutdown();
}

core::Result<void> RenderRuntimeHost::initWindow(const ViewConfig& config, int width, int height) {
    return runtime_.initWindow(config, width, height);
}

core::Result<void> RenderRuntimeHost::initOffscreen(int width, int height) {
    return runtime_.initOffscreen(width, height);
}

void RenderRuntimeHost::shutdown() {
    runtime_.shutdown();
    submission_builder_.reset();
    asset_source_ = nullptr;
}

bool RenderRuntimeHost::isInitialized() const {
    return runtime_.isInitialized();
}

void RenderRuntimeHost::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    if (assets != asset_source_) {
        runtime_.execute(ClearAssetResourcesCommand{});
    }
    asset_source_ = assets;
    submission_builder_.setScene(scene, assets);
}

void RenderRuntimeHost::setPreviewLayer(const PreviewLayer* preview) {
    submission_builder_.setPreviewLayer(preview);
}

void RenderRuntimeHost::setLightEnvironment(const engine::LightEnvironment& lightEnvironment) {
    submission_builder_.setLightEnvironment(lightEnvironment);
}

void RenderRuntimeHost::render(const ViewState& viewState) {
    RenderSubmission submission = submission_builder_.build(viewState);
    submission.surfaceGeneration = runtime_.surface().generation();
    runtime_.render(submission);
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
    return submission_builder_.lastStats();
}

const RenderSubmissionDiagnostics& RenderRuntimeHost::renderSubmissionDiagnostics() const {
    return submission_builder_.diagnostics();
}

const engine::RenderWorkloadStats& RenderRuntimeHost::lastRenderWorkloadStats() const {
    return runtime_.lastRenderWorkloadStats();
}

const engine::RenderCompilerStats& RenderRuntimeHost::lastRenderCompilerStats() const {
    return runtime_.lastRenderCompilerStats();
}

}  // namespace mulan::view
