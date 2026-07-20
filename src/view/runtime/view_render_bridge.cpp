#include "detail/view_render_bridge.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>

#include <utility>

namespace mulan::view::detail {

ViewRenderBridge::ViewRenderBridge() : session_(std::make_unique<engine::RenderSession>()) {
}

ViewRenderBridge::~ViewRenderBridge() {
    shutdown();
}

ResultVoid ViewRenderBridge::init(const ViewConfig& config, int width, int height,
                                  std::function<void()> runtimeEventCallback) {
    MULAN_PROFILE_ZONE();
    if (isReady())
        return {};
    auto initialized = session_->init(config.toRenderSessionConfig(), width, height, std::move(runtimeEventCallback));
    if (!initialized)
        return initialized;
    submission_builder_.invalidateResources();
    return {};
}

void ViewRenderBridge::shutdown() {
    session_->shutdown();
    submission_builder_.reset();
    asset_source_ = nullptr;
}

bool ViewRenderBridge::isReady() const {
    return session_->isReady();
}

ResultVoid ViewRenderBridge::consumeRuntimeEvents() {
    auto events = session_->consumeRuntimeEvents();
    if (!events) {
        submission_builder_.invalidateResources();
        return std::unexpected(events.error());
    }
    if (*events)
        submission_builder_.acknowledgeResources(**events);
    return {};
}

void ViewRenderBridge::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    if (assets != asset_source_) {
        if (auto cleared = session_->clearPersistentResources(); !cleared)
            LOG_ERROR("[ViewRenderBridge] Persistent resource clearing failed: {}", cleared.error().message);
        asset_source_ = assets;
    }
    submission_builder_.setScene(scene, assets);
}

void ViewRenderBridge::setPreviewLayer(const PreviewLayer* preview) {
    submission_builder_.setPreviewLayer(preview);
}

void ViewRenderBridge::setLightEnvironment(const engine::LightEnvironment& lightEnvironment) {
    submission_builder_.setLightEnvironment(lightEnvironment);
}

ResultVoid ViewRenderBridge::submitFrame(const ViewState& viewState) {
    auto submitted = session_->submitFrame(submission_builder_.build(viewState));
    if (!submitted && !session_->isReady())
        submission_builder_.invalidateResources();
    return submitted;
}

Result<engine::RenderCaptureResult> ViewRenderBridge::capture(const ViewState& viewState,
                                                              const engine::RenderCaptureDesc& desc) {
    auto result = session_->capture(submission_builder_.build(viewState), desc);
    auto consumed = consumeRuntimeEvents();
    if (!consumed)
        return std::unexpected(consumed.error());
    return result;
}

engine::RenderSurfaceState ViewRenderBridge::resize(int width, int height) {
    const auto state = session_->resize(width, height);
    if (!session_->isReady())
        submission_builder_.invalidateResources();
    return state;
}

void ViewRenderBridge::enableIBL(const std::string& hdrPath) {
    session_->enableIBL(hdrPath);
}

engine::RenderSurfaceState ViewRenderBridge::surfaceState() const {
    return session_->surfaceState();
}

}  // namespace mulan::view::detail
