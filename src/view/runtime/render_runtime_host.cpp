#include <mulan/view/runtime/render_runtime_host.h>

namespace mulan::view {

RenderRuntimeHost::~RenderRuntimeHost() {
    shutdown();
}

core::Result<void> RenderRuntimeHost::initWindow(const ViewConfig& config, int width, int height) {
    if (config.executionMode == RenderExecutionMode::Threaded) {
        threaded_runtime_ = std::make_unique<ThreadedRenderRuntime>();
        return threaded_runtime_->initWindow(config, width, height);
    }
    return runtime_->initWindow(config, width, height);
}

core::Result<void> RenderRuntimeHost::initOffscreen(const ViewConfig& config, int width, int height) {
    if (config.executionMode == RenderExecutionMode::Threaded) {
        threaded_runtime_ = std::make_unique<ThreadedRenderRuntime>();
        return threaded_runtime_->initOffscreen(config, width, height);
    }
    return runtime_->initOffscreen(config, width, height);
}

core::Result<void> RenderRuntimeHost::initOffscreen(int width, int height) {
    return initOffscreen(ViewConfig{}, width, height);
}

void RenderRuntimeHost::shutdown() {
    if (threaded_runtime_) {
        threaded_runtime_->shutdown();
        threaded_runtime_.reset();
    }
    runtime_->shutdown();
    submission_builder_.reset();
    asset_source_ = nullptr;
}

bool RenderRuntimeHost::isInitialized() const {
    return threaded_runtime_ ? threaded_runtime_->isInitialized() : runtime_->isInitialized();
}

void RenderRuntimeHost::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    if (assets != asset_source_) {
        if (threaded_runtime_)
            threaded_runtime_->clearAssetResources();
        else
            runtime_->clearAssetResources();
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
    submission.surfaceGeneration =
            threaded_runtime_ ? threaded_runtime_->surfaceGeneration() : runtime_->surface().generation();
    if (threaded_runtime_)
        threaded_runtime_->submitFrame(std::move(submission));
    else
        runtime_->render(submission);
}

core::Result<engine::RenderCaptureResult> RenderRuntimeHost::capture(const ViewState& viewState,
                                                                     const engine::RenderCaptureDesc& desc) {
    RenderSubmission submission = submission_builder_.build(viewState);
    submission.surfaceGeneration =
            threaded_runtime_ ? threaded_runtime_->surfaceGeneration() : runtime_->surface().generation();
    if (threaded_runtime_) {
        return threaded_runtime_->capture(std::move(submission), desc);
    }
    return runtime_->capture(submission, desc);
}

void RenderRuntimeHost::resize(int width, int height) {
    if (threaded_runtime_)
        threaded_runtime_->resize(width, height);
    else
        runtime_->resize(width, height);
}

void RenderRuntimeHost::enableIBL(const std::string& hdrPath) {
    if (threaded_runtime_)
        threaded_runtime_->enableIBL(hdrPath);
    else
        runtime_->enableIBL(hdrPath);
}

uint32_t RenderRuntimeHost::surfaceWidth() const {
    return threaded_runtime_ ? threaded_runtime_->surfaceWidth() : static_cast<uint32_t>(runtime_->surface().width());
}

uint32_t RenderRuntimeHost::surfaceHeight() const {
    return threaded_runtime_ ? threaded_runtime_->surfaceHeight() : static_cast<uint32_t>(runtime_->surface().height());
}

const RenderWorldSyncStats& RenderRuntimeHost::lastWorldSyncStats() const {
    return submission_builder_.lastStats();
}

const RenderSubmissionDiagnostics& RenderRuntimeHost::renderSubmissionDiagnostics() const {
    return submission_builder_.diagnostics();
}

const engine::RenderWorkloadStats& RenderRuntimeHost::lastRenderWorkloadStats() const {
    return runtime_->lastRenderWorkloadStats();
}

const engine::RenderCompilerStats& RenderRuntimeHost::lastRenderCompilerStats() const {
    return runtime_->lastRenderCompilerStats();
}

}  // namespace mulan::view
