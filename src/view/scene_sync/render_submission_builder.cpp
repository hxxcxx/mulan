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

void RenderSubmissionBuilder::reset() {
    scene_ = nullptr;
    assets_ = nullptr;
    preview_ = nullptr;
    last_scene_generation_ = 0;
    last_geometry_generation_ = 0;
    last_preview_generation_ = 0;
    submission_generation_ = 0;
    scene_source_dirty_ = true;
    preview_source_dirty_ = true;
    render_world_.clear();
    world_snapshot_.reset();
    last_sync_stats_ = {};
    diagnostics_ = {};
    light_environment_ = {};
    pending_prepare_.clear();
    resource_batch_id_ = 0;
}

void RenderSubmissionBuilder::setScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    if (scene_ == scene && assets_ == assets) {
        return;
    }

    scene_ = scene;
    assets_ = assets;
    invalidateResources();
}

void RenderSubmissionBuilder::setPreviewLayer(const PreviewLayer* preview) {
    if (preview_ == preview) {
        return;
    }

    preview_ = preview;
    invalidateResources();
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

    submission.rebuiltWorld = needsRebuild();
    if (submission.rebuiltWorld) {
        rebuild(submission);
    }

    if (!submission.prepare.empty()) {
        pending_prepare_.merge(submission.prepare);
        advanceResourceBatch();
    }
    submission.prepare = pending_prepare_;
    submission.resourceBatchId = pending_prepare_.empty() ? 0 : resource_batch_id_;

    submission.world = world_snapshot_;
    submission.syncStats = last_sync_stats_;
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
    diagnostics_.lastResourceUpdateCount = submission.prepare.geometries().size();
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
    scene_source_dirty_ = true;
    preview_source_dirty_ = true;
}

bool RenderSubmissionBuilder::needsRebuild() const {
    if (scene_source_dirty_ || preview_source_dirty_) {
        return true;
    }

    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
    return sceneGeneration != last_scene_generation_ || previewGeneration != last_preview_generation_;
}

void RenderSubmissionBuilder::rebuild(RenderSubmission& submission) {
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    const uint64_t geometryGeneration = scene_ ? scene_->geometryGeneration() : 0;
    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;

    if (!scene_ || !assets_) {
        render_world_.clear();
        world_snapshot_.reset();
        last_sync_stats_ = {};
    } else {
        const bool sceneGeometryChanged = scene_source_dirty_ || geometryGeneration != last_geometry_generation_;
        render_world_sync_.rebuild(*scene_, *assets_, preview_, render_world_, &submission.prepare,
                                   sceneGeometryChanged, sceneGeometryChanged);
        world_snapshot_ = std::make_shared<engine::RenderWorldSnapshot>(render_world_.snapshot());
        last_sync_stats_ = render_world_sync_.lastStats();
    }

    last_scene_generation_ = sceneGeneration;
    last_geometry_generation_ = geometryGeneration;
    last_preview_generation_ = previewGeneration;
    scene_source_dirty_ = false;
    preview_source_dirty_ = false;
}

void RenderSubmissionBuilder::advanceResourceBatch() {
    ++resource_batch_id_;
    if (resource_batch_id_ == 0) {
        resource_batch_id_ = 1;
    }
}

}  // namespace mulan::view
