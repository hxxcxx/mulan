/**
 * @file render_submission_builder.cpp
 * @brief RenderSubmissionBuilder 实现。
 * @author hxxcxx
 * @date 2026-07-10
 */

#include "scene_sync/render_submission_builder.h"

#include <mulan/view/core/preview_layer.h>
#include <mulan/view/scene_sync/render_scene.h>

#include <mulan/asset/asset_library.h>

#include <utility>

namespace mulan::view {

RenderSubmissionBuilder::RenderSubmissionBuilder()
    : preview_resource_domain_(engine::allocateTransientResourceDomain()) {
}

void RenderSubmissionBuilder::reset() {
    scene_ = nullptr;
    assets_ = nullptr;
    asset_resource_domain_ = {};
    preview_ = nullptr;
    last_scene_generation_ = 0;
    last_preview_generation_ = 0;
    last_overlay_scene_generation_ = 0;
    submission_generation_ = 0;
    scene_source_dirty_ = true;
    preview_source_dirty_ = true;
    scene_world_.clear();
    overlay_world_.clear();
    scene_world_snapshot_.reset();
    overlay_world_snapshot_.reset();
    last_scene_sync_stats_ = {};
    last_overlay_sync_stats_ = {};
    diagnostics_ = {};
    light_environment_ = {};
    pending_prepare_.clear();
    resource_batch_id_ = 0;
    scene_world_sync_.reset();
    overlay_world_sync_.reset();
}

void RenderSubmissionBuilder::setScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    if (scene_ == scene && assets_ == assets) {
        return;
    }

    const bool assetDomainChanged = assets_ != assets;
    scene_ = scene;
    assets_ = assets;
    asset_resource_domain_ =
            assets ? engine::resourceDomainForAssetLibrary(assets->domainId()) : engine::ResourceDomainId{};
    if (assetDomainChanged) {
        invalidateResources();
    } else {
        // 同一资产域内切换 scene 时保留 key/revision 基线，仅对真实差量上传或退役。
        scene_source_dirty_ = true;
        // PreviewReference 依赖 SceneProxy，Scene 换源时覆盖层也必须重建。
        preview_source_dirty_ = true;
    }
}

void RenderSubmissionBuilder::setPreviewLayer(const PreviewLayer* preview) {
    if (preview_ == preview) {
        return;
    }

    preview_ = preview;
    // PreviewLayer 不定义 GPU 执行域；换源由稳定预览 key 生成差量，
    // 不应连带强制重传全部场景几何。
    overlay_world_sync_.invalidatePreviewResources();
    preview_source_dirty_ = true;
}

void RenderSubmissionBuilder::setLightEnvironment(const engine::LightEnvironment& lightEnvironment) {
    light_environment_ = lightEnvironment;
}

RenderSubmission RenderSubmissionBuilder::build(const ViewState& viewState) {
    RenderSubmission submission;
    submission.view = viewState;
    submission.lightEnvironment = light_environment_;
    submission.sceneGeneration = scene_ ? scene_->generation() : 0;
    submission.geometryGeneration = scene_ ? scene_->geometryGeneration() : 0;
    submission.previewGeneration = preview_ ? preview_->generation() : 0;

    submission.rebuiltSceneWorld = needsSceneRebuild();
    submission.rebuiltOverlayWorld = needsOverlayRebuild();
    submission.rebuiltWorld = submission.rebuiltSceneWorld || submission.rebuiltOverlayWorld;
    if (submission.rebuiltSceneWorld) {
        rebuildScene(submission);
    }
    if (submission.rebuiltOverlayWorld) {
        rebuildOverlay(submission);
    }

    if (!submission.prepare.empty()) {
        pending_prepare_.merge(submission.prepare);
        advanceResourceBatch();
    }
    submission.prepare = pending_prepare_;
    submission.resourceBatchId = pending_prepare_.empty() ? 0 : resource_batch_id_;

    submission.sceneWorld = scene_world_snapshot_;
    submission.overlayWorld = overlay_world_snapshot_;
    submission.sceneSyncStats = last_scene_sync_stats_;
    submission.overlaySyncStats = last_overlay_sync_stats_;
    submission.generation = ++submission_generation_;
    if (submission_generation_ == 0) {
        submission_generation_ = 1;
        submission.generation = submission_generation_;
    }
    ++diagnostics_.submissionCount;
    if (submission.rebuiltWorld) {
        ++diagnostics_.worldRebuildCount;
    } else {
        ++diagnostics_.worldReuseCount;
    }
    if (submission.rebuiltSceneWorld) {
        ++diagnostics_.sceneWorldRebuildCount;
    } else {
        ++diagnostics_.sceneWorldReuseCount;
    }
    if (submission.rebuiltOverlayWorld) {
        ++diagnostics_.overlayWorldRebuildCount;
    } else {
        ++diagnostics_.overlayWorldReuseCount;
    }
    diagnostics_.lastResourceUpdateCount = submission.prepare.size();
    diagnostics_.lastSceneGeneration = submission.sceneGeneration;
    diagnostics_.lastGeometryGeneration = submission.geometryGeneration;
    diagnostics_.lastPreviewGeneration = submission.previewGeneration;
    return submission;
}

void RenderSubmissionBuilder::acknowledgeResources(uint64_t batchId) {
    if (batchId == 0 || batchId != resource_batch_id_) {
        return;
    }
    pending_prepare_.clear();
}

void RenderSubmissionBuilder::invalidateResources() {
    pending_prepare_.clear();
    advanceResourceBatch();
    scene_world_sync_.invalidateResources();
    overlay_world_sync_.invalidateResources();
    scene_source_dirty_ = true;
    preview_source_dirty_ = true;
}

bool RenderSubmissionBuilder::needsSceneRebuild() const {
    if (scene_source_dirty_) {
        return true;
    }

    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    if (sceneGeneration != last_scene_generation_) {
        return true;
    }
    return scene_ && assets_ && scene_world_sync_.referencedAssetsChanged(*assets_);
}

bool RenderSubmissionBuilder::needsOverlayRebuild() const {
    if (preview_source_dirty_) {
        return true;
    }

    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
    if (previewGeneration != last_preview_generation_) {
        return true;
    }
    // 只有引用场景实体的预览才依赖 SceneProxy generation；直接预览几何完全独立。
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    if (preview_ && !preview_->references().empty() && sceneGeneration != last_overlay_scene_generation_) {
        return true;
    }
    return assets_ && overlay_world_sync_.referencedAssetsChanged(*assets_);
}

void RenderSubmissionBuilder::rebuildScene(RenderSubmission& submission) {
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    engine::RenderResourcePrepareList prepare;

    if (!scene_ || !assets_) {
        scene_world_sync_.rebuildEmpty(scene_world_, &prepare);
        scene_world_snapshot_.reset();
        last_scene_sync_stats_ = scene_world_sync_.lastStats();
    } else {
        scene_world_sync_.rebuildScene(*scene_, *assets_, asset_resource_domain_, scene_world_, &prepare);
        scene_world_snapshot_ = std::make_shared<engine::RenderWorldSnapshot>(scene_world_.snapshot());
        last_scene_sync_stats_ = scene_world_sync_.lastStats();
    }
    submission.prepare.merge(prepare);

    last_scene_generation_ = sceneGeneration;
    scene_source_dirty_ = false;
}

void RenderSubmissionBuilder::rebuildOverlay(RenderSubmission& submission) {
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
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

    last_preview_generation_ = previewGeneration;
    last_overlay_scene_generation_ = sceneGeneration;
    preview_source_dirty_ = false;
}

void RenderSubmissionBuilder::advanceResourceBatch() {
    ++resource_batch_id_;
    if (resource_batch_id_ == 0) {
        resource_batch_id_ = 1;
    }
}

}  // namespace mulan::view
