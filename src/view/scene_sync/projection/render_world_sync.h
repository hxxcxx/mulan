/**
 * @file render_world_sync.h
 * @brief RenderWorldSync 将 view 的 RenderScene/AssetLibrary 同步到 engine RenderWorld。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_item_builder.h"
#include "render_resource_delta.h"

#include <mulan/asset/asset_id.h>
#include <mulan/asset/asset_change.h>
#include <mulan/render/frontend/render_world.h>
#include <mulan/render/frontend/render_resource_prepare.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class RenderScene;
class PreviewLayer;
}  // namespace mulan::view

namespace mulan::view {

struct RenderWorldSyncStats {
    bool fullRebuild = false;
    size_t patchedObjectCount = 0;
    size_t addedObjectCount = 0;
    size_t updatedObjectCount = 0;
    size_t removedObjectCount = 0;
    size_t sceneProxyCount = 0;
    size_t missingGeometryAssetCount = 0;
    size_t sceneObjectCount = 0;
    size_t previewObjectCount = 0;
    size_t worldObjectCount = 0;
    size_t worldGeometryCount = 0;
    size_t worldMaterialCount = 0;
    RenderItemDiagnostics sceneItems;
    RenderItemDiagnostics previewItems;

    void reset() { *this = {}; }
};

class RenderWorldSync {
public:
    RenderWorldSync();
    ~RenderWorldSync();

    RenderWorldSync(const RenderWorldSync&) = delete;
    RenderWorldSync& operator=(const RenderWorldSync&) = delete;

    /// 重建稳定文档场景，不包含工具预览和选择视觉状态。
    void rebuildScene(const RenderScene& scene, const asset::AssetLibrary& assets, engine::ResourceDomainId assetDomain,
                      engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare = nullptr);

    /// 按当前预览 generation 整体重建小型 OverlayWorld；资产引用复用 SceneWorld 的 GPU key。
    void rebuildOverlay(const RenderScene* scene, const asset::AssetLibrary* assets,
                        engine::ResourceDomainId assetDomain, engine::ResourceDomainId previewDomain,
                        const PreviewLayer* preview, engine::RenderWorld& world,
                        engine::RenderResourcePrepareList* prepare = nullptr);

    /// 将空世界与已记录的 GPU 几何/贴图做差量同步，用于 scene 来源消失。
    void rebuildEmpty(engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare = nullptr);

    /// 已引用资产的内容版本是否已超过上次 world 快照。
    /// 仅包含无关事件时会确认其 journal 游标，避免无关变更挤出保留窗口后误判全量。
    bool referencedAssetsChanged(const asset::AssetLibrary& assets) const;

    /// 渲染线程重建后保留 CPU 差量基线，但下次 rebuild 必须完整重传当前存活资源。
    void invalidateResources() { resource_delta_.invalidate(); }

    /// 丢弃所有 world/resource 基线，仅用于上层整体 reset。
    void reset();

    const RenderWorldSyncStats& lastStats() const { return last_stats_; }
    void clearStats() { last_stats_.reset(); }

private:
    struct SceneState;

    RenderWorldSyncStats last_stats_;
    detail::RenderResourceDelta resource_delta_;
    std::unordered_map<asset::AssetId, uint64_t> referenced_asset_revisions_;
    mutable asset::AssetChangeCursor asset_change_cursor_;
    std::unique_ptr<SceneState> scene_state_;
};

}  // namespace mulan::view
