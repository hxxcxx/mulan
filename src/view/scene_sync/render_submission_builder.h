/**
 * @file render_submission_builder.h
 * @brief RenderSubmissionBuilder 将 view 活对象收口为自持有渲染提交。
 * @author hxxcxx
 * @date 2026-07-10
 */

#pragma once

#include <mulan/view/scene_sync/render_submission.h>

#include <mulan/render/frontend/render_world.h>
#include <mulan/render/light_environment.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class PreviewLayer;
class RenderScene;

struct RenderSubmissionDiagnostics {
    uint64_t submissionCount = 0;
    uint64_t worldRebuildCount = 0;
    uint64_t worldReuseCount = 0;
    size_t lastResourceUpdateCount = 0;
    uint64_t lastSceneGeneration = 0;
    uint64_t lastGeometryGeneration = 0;
    uint64_t lastPreviewGeneration = 0;
};

class RenderSubmissionBuilder {
public:
    void reset();
    void setScene(const RenderScene* scene, const asset::AssetLibrary* assets);
    void setPreviewLayer(const PreviewLayer* preview);
    void setLightEnvironment(const engine::LightEnvironment& lightEnvironment);

    RenderSubmission build(const ViewState& viewState);

    const RenderWorldSyncStats& lastStats() const { return last_sync_stats_; }
    const RenderSubmissionDiagnostics& diagnostics() const { return diagnostics_; }

private:
    bool needsRebuild() const;
    void rebuild(RenderSubmission& submission);

    const RenderScene* scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;
    const PreviewLayer* preview_ = nullptr;

    uint64_t last_scene_generation_ = 0;
    uint64_t last_geometry_generation_ = 0;
    uint64_t last_preview_generation_ = 0;
    uint64_t submission_generation_ = 0;
    bool scene_source_dirty_ = true;
    bool preview_source_dirty_ = true;

    RenderWorldSync render_world_sync_;
    engine::RenderWorld render_world_;
    std::shared_ptr<const engine::RenderWorldSnapshot> world_snapshot_;
    RenderWorldSyncStats last_sync_stats_;
    RenderSubmissionDiagnostics diagnostics_;
    engine::LightEnvironment light_environment_;
};

}  // namespace mulan::view
