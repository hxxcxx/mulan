/**
 * @file render_submission_builder.cpp
 * @brief RenderSubmissionBuilder 实现。
 * @author hxxcxx
 * @date 2026-07-10
 */

#include "render_submission_builder.h"
#include "../render_scene.h"
#include "../../core/preview_layer.h"

#include <mulan/asset/asset_library.h>
#include <mulan/core/profiling/profile.h>

namespace mulan::view {
namespace {

uint64_t sceneChangeDomain(const RenderScene* scene) {
    return scene ? scene->currentChangeCursor().domain : 0;
}

engine::DisplayMode toDisplayMode(RenderMode mode) {
    switch (mode) {
    case RenderMode::Shaded: return engine::DisplayMode::Shaded;
    case RenderMode::ShadedWithEdges: return engine::DisplayMode::ShadedWithEdges;
    case RenderMode::Wireframe: return engine::DisplayMode::Wireframe;
    }
    return engine::DisplayMode::ShadedWithEdges;
}

void applyViewState(engine::RenderFrameSubmission& submission, const ViewState& state) {
    submission.view.viewMatrix = state.viewMatrix;
    submission.view.projectionMatrix = state.projectionMatrix;
    submission.view.cameraPosition = state.cameraPosition;
    submission.view.width = static_cast<uint32_t>(state.width);
    submission.view.height = static_cast<uint32_t>(state.height);
    submission.options.displayMode = toDisplayMode(state.renderMode);
    submission.options.hoveredPickId = state.hoveredPickId;
    submission.options.selectionVisuals = state.selectionVisuals;
    submission.options.showSurfaces = state.showFaces;
    submission.options.showEdges = state.showEdges;
    submission.options.showOverlays = state.showOverlays;
    submission.options.showViewCube = state.showViewCube;
    submission.options.viewCubeLayout = state.viewCubeLayout;
    submission.options.viewCubeInteraction = state.viewCubeInteraction;
}
}  // namespace

RenderSubmissionBuilder::RenderSubmissionBuilder()
    : preview_resource_domain_(engine::allocateTransientResourceDomain()) {
}

void RenderSubmissionBuilder::reset() {
    scene_ = nullptr;
    assets_ = nullptr;
    asset_resource_domain_ = {};
    preview_ = nullptr;
    bound_scene_change_domain_ = 0;

    scene_projection_.generation = 0;
    scene_projection_.changeDomain = 0;
    scene_projection_.sourceDirty = true;
    scene_projection_.world.clear();
    scene_projection_.snapshot.reset();
    scene_projection_.stats = {};
    scene_projection_.sync.reset();

    overlay_projection_.previewGeneration = 0;
    overlay_projection_.sceneGeneration = 0;
    overlay_projection_.sceneChangeDomain = 0;
    overlay_projection_.previewSourceDirty = true;
    overlay_projection_.referenceSourceDirty = true;
    overlay_projection_.world.clear();
    overlay_projection_.snapshot.reset();
    overlay_projection_.stats = {};
    overlay_projection_.sync.reset();

    light_environment_ = {};
    resource_outbox_.reset();
}

void RenderSubmissionBuilder::setScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    const uint64_t nextSceneDomain = sceneChangeDomain(scene);
    const engine::ResourceDomainId nextAssetDomain =
            assets ? engine::resourceDomainForAssetLibrary(assets->domainId()) : engine::ResourceDomainId{};
    if (scene_ == scene && assets_ == assets && bound_scene_change_domain_ == nextSceneDomain &&
        asset_resource_domain_ == nextAssetDomain) {
        return;
    }

    // 裸指针可能复用地址；Scene change-domain 与资产 resource-domain 才是长期来源身份。
    const bool assetDomainChanged = asset_resource_domain_ != nextAssetDomain;
    scene_ = scene;
    assets_ = assets;
    bound_scene_change_domain_ = nextSceneDomain;
    asset_resource_domain_ = nextAssetDomain;
    if (assetDomainChanged) {
        invalidateResources();
    } else {
        scene_projection_.sourceDirty = true;
        overlay_projection_.referenceSourceDirty = true;
    }
}

void RenderSubmissionBuilder::setPreviewLayer(const PreviewLayer* preview) {
    if (preview_ == preview)
        return;
    preview_ = preview;
    // 预览 key 按角色槽位稳定复用；换源时强制覆盖当前存活的小型资源集。
    overlay_projection_.sync.invalidateResources();
    overlay_projection_.previewSourceDirty = true;
}

void RenderSubmissionBuilder::setLightEnvironment(const engine::LightEnvironment& lightEnvironment) {
    light_environment_ = lightEnvironment;
}

engine::RenderFrameSubmission RenderSubmissionBuilder::build(const ViewState& viewState) {
    MULAN_PROFILE_ZONE();

    engine::RenderFrameSubmission submission;
    applyViewState(submission, viewState);
    submission.lighting = light_environment_;
    if (needsSceneRebuild())
        rebuildScene(submission);
    if (needsOverlayRebuild())
        rebuildOverlay(submission);
    finalizeSubmission(submission);
    return submission;
}

void RenderSubmissionBuilder::acknowledgeResources(uint64_t batchId) {
    resource_outbox_.acknowledge(batchId);
}

void RenderSubmissionBuilder::invalidateResources() {
    resource_outbox_.invalidate();
    scene_projection_.sync.invalidateResources();
    overlay_projection_.sync.invalidateResources();
    scene_projection_.sourceDirty = true;
    overlay_projection_.previewSourceDirty = true;
    overlay_projection_.referenceSourceDirty = true;
}

bool RenderSubmissionBuilder::needsSceneRebuild() const {
    MULAN_PROFILE_ZONE();

    if (scene_projection_.sourceDirty)
        return true;
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    if (sceneGeneration != scene_projection_.generation ||
        sceneChangeDomain(scene_) != scene_projection_.changeDomain) {
        return true;
    }
    return scene_ && assets_ && scene_projection_.sync.referencedAssetsChanged(*assets_);
}

bool RenderSubmissionBuilder::needsOverlayRebuild() const {
    MULAN_PROFILE_ZONE();

    if (overlay_projection_.previewSourceDirty || overlay_projection_.referenceSourceDirty)
        return true;
    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
    if (previewGeneration != overlay_projection_.previewGeneration)
        return true;
    if (preview_ && !preview_->references().empty()) {
        const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
        if (sceneGeneration != overlay_projection_.sceneGeneration ||
            sceneChangeDomain(scene_) != overlay_projection_.sceneChangeDomain) {
            return true;
        }
    }
    return preview_ && !preview_->references().empty() && assets_ &&
           overlay_projection_.sync.referencedAssetsChanged(*assets_);
}

void RenderSubmissionBuilder::rebuildScene(engine::RenderFrameSubmission& submission) {
    MULAN_PROFILE_ZONE();

    engine::RenderResourcePrepareList prepare;
    if (!scene_ || !assets_) {
        scene_projection_.sync.rebuildEmpty(scene_projection_.world, &prepare);
        scene_projection_.snapshot.reset();
    } else {
        scene_projection_.sync.rebuildScene(*scene_, *assets_, asset_resource_domain_, scene_projection_.world,
                                            &prepare);
        scene_projection_.snapshot = std::make_shared<engine::RenderWorldSnapshot>(scene_projection_.world.snapshot());
    }
    scene_projection_.stats = scene_projection_.sync.lastStats();
    submission.prepare.merge(prepare);
    scene_projection_.generation = scene_ ? scene_->generation() : 0;
    scene_projection_.changeDomain = sceneChangeDomain(scene_);
    scene_projection_.sourceDirty = false;
}

void RenderSubmissionBuilder::rebuildOverlay(engine::RenderFrameSubmission& submission) {
    MULAN_PROFILE_ZONE();

    engine::RenderResourcePrepareList prepare;
    overlay_projection_.sync.rebuildOverlay(scene_, assets_, asset_resource_domain_, preview_resource_domain_, preview_,
                                            overlay_projection_.world, &prepare);
    if (overlay_projection_.world.objectCount() == 0) {
        overlay_projection_.snapshot.reset();
    } else {
        overlay_projection_.snapshot =
                std::make_shared<engine::RenderWorldSnapshot>(overlay_projection_.world.snapshot());
    }
    overlay_projection_.stats = overlay_projection_.sync.lastStats();
    submission.prepare.merge(prepare);
    overlay_projection_.previewGeneration = preview_ ? preview_->generation() : 0;
    overlay_projection_.sceneGeneration = scene_ ? scene_->generation() : 0;
    overlay_projection_.sceneChangeDomain = sceneChangeDomain(scene_);
    overlay_projection_.previewSourceDirty = false;
    overlay_projection_.referenceSourceDirty = false;
}

void RenderSubmissionBuilder::finalizeSubmission(engine::RenderFrameSubmission& submission) {
    MULAN_PROFILE_ZONE();

    resource_outbox_.merge(submission.prepare);
    resource_outbox_.attachTo(submission);
    submission.sceneWorld = scene_projection_.snapshot;
    submission.overlayWorld = overlay_projection_.snapshot;
}

}  // namespace mulan::view
