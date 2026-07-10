/**
 * @file render_submission_builder.cpp
 * @brief RenderSubmissionBuilder 实现。
 * @author hxxcxx
 * @date 2026-07-10
 */

#include "render_submission_builder.h"

#include "preview_layer.h"
#include "render_scene.h"

#include <mulan/asset/asset_library.h>

#include <utility>

namespace mulan::view {

void RenderSubmissionBuilder::setScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    if (scene_ == scene && assets_ == assets) {
        return;
    }

    scene_ = scene;
    assets_ = assets;
    scene_source_dirty_ = true;
}

void RenderSubmissionBuilder::setPreviewLayer(const PreviewLayer* preview) {
    if (preview_ == preview) {
        return;
    }

    preview_ = preview;
    preview_source_dirty_ = true;
}

RenderSubmission RenderSubmissionBuilder::build(const ViewState& viewState) {
    RenderSubmission submission;
    submission.view = viewState;

    if (needsRebuild()) {
        rebuild(submission);
    }

    submission.world = world_snapshot_;
    submission.syncStats = last_sync_stats_;
    submission.generation = ++submission_generation_;
    if (submission_generation_ == 0) {
        submission_generation_ = 1;
        submission.generation = submission_generation_;
    }
    return submission;
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

}  // namespace mulan::view
