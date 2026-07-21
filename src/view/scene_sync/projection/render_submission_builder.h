/**
 * @file render_submission_builder.h
 * @brief RenderSubmissionBuilder 将 view 活对象收口为自持有渲染提交。
 * @author hxxcxx
 * @date 2026-07-10
 */

#pragma once

#include "../../core/view_state.h"
#include "render_resource_outbox.h"
#include "render_world_sync.h"

#include <mulan/render/frontend/render_frame_submission.h>
#include <mulan/render/frontend/render_world.h>
#include <mulan/render/light_environment.h>

#include <cstdint>
#include <memory>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class PreviewLayer;
class RenderScene;

class RenderSubmissionBuilder {
public:
    RenderSubmissionBuilder();

    void reset();
    void setScene(const RenderScene* scene, const asset::AssetLibrary* assets);
    void setPreviewLayer(const PreviewLayer* preview);
    void setLightEnvironment(const engine::LightEnvironment& lightEnvironment);

    engine::RenderFrameSubmission build(const ViewState& viewState);

    /// 仅在执行端确认整个上传批次后清除 pending；过期 ACK 不影响更新批次。
    void acknowledgeResources(uint64_t batchId);
    /// 渲染线程重建或资产域切换后，强制从当前 CPU 场景重新生成资源批次。
    void invalidateResources();

    const RenderWorldSyncStats& lastStats() const { return scene_projection_.stats; }
    const RenderWorldSyncStats& lastOverlayStats() const { return overlay_projection_.stats; }

private:
    struct SceneProjectionState {
        RenderWorldSync sync;
        engine::RenderWorld world;
        std::shared_ptr<const engine::RenderWorldSnapshot> snapshot;
        RenderWorldSyncStats stats;
        uint64_t generation = 0;
        uint64_t changeDomain = 0;
        bool sourceDirty = true;
    };

    struct OverlayProjectionState {
        RenderWorldSync sync;
        engine::RenderWorld world;
        std::shared_ptr<const engine::RenderWorldSnapshot> snapshot;
        RenderWorldSyncStats stats;
        uint64_t previewGeneration = 0;
        uint64_t sceneGeneration = 0;
        uint64_t sceneChangeDomain = 0;
        bool previewSourceDirty = true;
        bool referenceSourceDirty = true;
    };

    bool needsSceneRebuild() const;
    bool needsOverlayRebuild() const;
    void rebuildScene(engine::RenderFrameSubmission& submission);
    void rebuildOverlay(engine::RenderFrameSubmission& submission);
    void finalizeSubmission(engine::RenderFrameSubmission& submission);

    const RenderScene* scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;
    const PreviewLayer* preview_ = nullptr;

    /// 当前绑定来源的 change-domain，用于阻断同址换代 ABA。
    uint64_t bound_scene_change_domain_ = 0;

    SceneProjectionState scene_projection_;
    OverlayProjectionState overlay_projection_;
    engine::LightEnvironment light_environment_;
    RenderResourceOutbox resource_outbox_;
    engine::ResourceDomainId asset_resource_domain_;
    engine::ResourceDomainId preview_resource_domain_;
};

}  // namespace mulan::view
