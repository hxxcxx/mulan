/**
 * @file render_submission_builder.cpp
 * @brief RenderSubmissionBuilder 实现。
 * @author hxxcxx
 * @date 2026-07-10
 */

#include "scene_sync/render_submission_builder.h"

#include <mulan/asset/asset_library.h>
#include <mulan/view/core/preview_layer.h>
#include <mulan/view/scene_sync/render_scene.h>

namespace mulan::view {
namespace {

uint64_t sceneChangeDomain(const RenderScene* scene) {
    return scene ? scene->currentChangeCursor().domain : 0;
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
    last_scene_generation_ = 0;
    bound_scene_change_domain_ = 0;
    last_scene_change_domain_ = 0;
    last_overlay_scene_change_domain_ = 0;
    last_preview_generation_ = 0;
    last_overlay_scene_generation_ = 0;
    scene_source_dirty_ = true;
    preview_source_dirty_ = true;
    overlay_reference_source_dirty_ = true;
    scene_world_.clear();
    overlay_world_.clear();
    scene_world_snapshot_.reset();
    overlay_world_snapshot_.reset();
    last_scene_sync_stats_ = {};
    last_overlay_sync_stats_ = {};
    light_environment_ = {};
    pending_prepare_.clear();
    resource_batch_id_ = 0;
    scene_world_sync_.reset();
    overlay_world_sync_.reset();
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
        scene_source_dirty_ = true;
        overlay_reference_source_dirty_ = true;
    }
}

void RenderSubmissionBuilder::setPreviewLayer(const PreviewLayer* preview) {
    if (preview_ == preview)
        return;
    preview_ = preview;
    // 预览 key 按角色槽位稳定复用；换源时强制覆盖当前存活的小型资源集。
    overlay_world_sync_.invalidateResources();
    preview_source_dirty_ = true;
}

void RenderSubmissionBuilder::setLightEnvironment(const engine::LightEnvironment& lightEnvironment) {
    light_environment_ = lightEnvironment;
}

RenderSubmission RenderSubmissionBuilder::build(const ViewState& viewState) {
    RenderSubmission submission;
    submission.view = viewState;
    submission.lightEnvironment = light_environment_;
    if (needsSceneRebuild())
        rebuildScene(submission);
    if (needsOverlayRebuild())
        rebuildOverlay(submission);

    if (!submission.prepare.empty()) {
        pending_prepare_.merge(submission.prepare);
        advanceResourceBatch();
    }
    submission.prepare = pending_prepare_;
    submission.resourceBatchId = pending_prepare_.empty() ? 0 : resource_batch_id_;
    submission.sceneWorld = scene_world_snapshot_;
    submission.overlayWorld = overlay_world_snapshot_;
    return submission;
}

void RenderSubmissionBuilder::acknowledgeResources(uint64_t batchId) {
    if (batchId != 0 && batchId == resource_batch_id_)
        pending_prepare_.clear();
}

void RenderSubmissionBuilder::invalidateResources() {
    pending_prepare_.clear();
    advanceResourceBatch();
    scene_world_sync_.invalidateResources();
    overlay_world_sync_.invalidateResources();
    scene_source_dirty_ = true;
    preview_source_dirty_ = true;
    overlay_reference_source_dirty_ = true;
}

bool RenderSubmissionBuilder::needsSceneRebuild() const {
    if (scene_source_dirty_)
        return true;
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    if (sceneGeneration != last_scene_generation_ || sceneChangeDomain(scene_) != last_scene_change_domain_)
        return true;
    return scene_ && assets_ && scene_world_sync_.referencedAssetsChanged(*assets_);
}

bool RenderSubmissionBuilder::needsOverlayRebuild() const {
    if (preview_source_dirty_ || overlay_reference_source_dirty_)
        return true;
    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
    if (previewGeneration != last_preview_generation_)
        return true;
    if (preview_ && !preview_->references().empty()) {
        const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
        if (sceneGeneration != last_overlay_scene_generation_ ||
            sceneChangeDomain(scene_) != last_overlay_scene_change_domain_) {
            return true;
        }
    }
    return preview_ && !preview_->references().empty() && assets_ &&
           overlay_world_sync_.referencedAssetsChanged(*assets_);
}

void RenderSubmissionBuilder::rebuildScene(RenderSubmission& submission) {
    engine::RenderResourcePrepareList prepare;
    if (!scene_ || !assets_) {
        scene_world_sync_.rebuildEmpty(scene_world_, &prepare);
        scene_world_snapshot_.reset();
    } else {
        scene_world_sync_.rebuildScene(*scene_, *assets_, asset_resource_domain_, scene_world_, &prepare);
        scene_world_snapshot_ = std::make_shared<engine::RenderWorldSnapshot>(scene_world_.snapshot());
    }
    last_scene_sync_stats_ = scene_world_sync_.lastStats();
    submission.prepare.merge(prepare);
    last_scene_generation_ = scene_ ? scene_->generation() : 0;
    last_scene_change_domain_ = sceneChangeDomain(scene_);
    scene_source_dirty_ = false;
}

void RenderSubmissionBuilder::rebuildOverlay(RenderSubmission& submission) {
    engine::RenderResourcePrepareList prepare;
    overlay_world_sync_.rebuildOverlay(scene_, assets_, asset_resource_domain_, preview_resource_domain_, preview_,
                                       overlay_world_, &prepare);
    if (overlay_world_.objectCount() == 0) {
        overlay_world_snapshot_.reset();
    } else {
        overlay_world_snapshot_ = std::make_shared<engine::RenderWorldSnapshot>(overlay_world_.snapshot());
    }
    last_overlay_sync_stats_ = overlay_world_sync_.lastStats();
    submission.prepare.merge(prepare);
    last_preview_generation_ = preview_ ? preview_->generation() : 0;
    last_overlay_scene_generation_ = scene_ ? scene_->generation() : 0;
    last_overlay_scene_change_domain_ = sceneChangeDomain(scene_);
    preview_source_dirty_ = false;
    overlay_reference_source_dirty_ = false;
}

void RenderSubmissionBuilder::advanceResourceBatch() {
    if (++resource_batch_id_ == 0)
        resource_batch_id_ = 1;
}

}  // namespace mulan::view
