/**
 * @file render_world_sync.h
 * @brief RenderWorldSync 将 view 的 RenderScene/AssetLibrary 同步到 engine RenderWorld。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "scene_sync/render_item_builder.h"

#include <mulan/asset/asset_id.h>
#include <mulan/render/frontend/render_world.h>
#include <mulan/render/frontend/render_resource_prepare.h>

#include <array>
#include <cstddef>
#include <cstdint>
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
    /// 重建稳定文档场景，不包含工具预览和选择视觉状态。
    void rebuildScene(const RenderScene& scene, const asset::AssetLibrary& assets, engine::RenderWorld& world,
                      engine::RenderResourcePrepareList* prepare = nullptr);

    /// 重建高频覆盖层。预览引用复用 SceneWorld 已准备的资产几何，只管理预览自有几何资源。
    void rebuildOverlay(const RenderScene* scene, const asset::AssetLibrary* assets, const PreviewLayer* preview,
                        engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare = nullptr);

    /// 将空世界与已记录的 GPU 几何/贴图做差量同步，用于 scene 来源消失。
    void rebuildEmpty(engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare = nullptr);

    /// 已引用资产的内容版本是否已超过上次 world 快照。
    bool referencedAssetsChanged(const asset::AssetLibrary& assets) const;

    /// GPU 执行域丢失后保留 CPU 差量基线，但下次 rebuild 必须完整重传当前存活资源。
    void invalidateResources() { force_full_prepare_ = true; }

    /// PreviewLayer 指针换源时只使当前同步器的预览 key 内容基线失效。
    void invalidatePreviewResources();

    /// 丢弃所有 world/resource 基线，仅用于上层整体 reset。
    void reset();

    const RenderWorldSyncStats& lastStats() const { return last_stats_; }
    void clearStats() { last_stats_.reset(); }

private:
    RenderWorldSyncStats last_stats_;
    // [0]=内容域（0=资产，非 0=预览换源世代），[1]=源版本，[2..3]=mesh 内容指纹。
    std::unordered_map<engine::AssetGpuKey, std::array<uint64_t, 4>> geometry_revisions_;
    std::unordered_map<engine::RenderTextureResourceKey, uint64_t, engine::RenderTextureResourceKeyHash>
            texture_revisions_;
    std::unordered_map<asset::AssetId, uint64_t> referenced_asset_revisions_;
    uint64_t preview_source_revision_ = 1;
    bool force_full_prepare_ = true;
};

}  // namespace mulan::view
