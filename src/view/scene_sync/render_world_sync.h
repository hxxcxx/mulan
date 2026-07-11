/**
 * @file render_world_sync.h
 * @brief RenderWorldSync 将 view 的 RenderScene/AssetLibrary 同步到 engine RenderWorld。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <mulan/view/scene_sync/render_item_builder.h>

#include <mulan/render/frontend/render_world.h>
#include <mulan/render/frontend/render_resource_prepare.h>

#include <cstddef>

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
    void rebuild(const RenderScene& scene, const asset::AssetLibrary& assets, const PreviewLayer* preview,
                 engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare = nullptr,
                 bool prepareSceneGeometry = true, bool forceSceneGeometryUpdate = false) const;

    const RenderWorldSyncStats& lastStats() const { return last_stats_; }
    void clearStats() const { last_stats_.reset(); }

private:
    mutable RenderWorldSyncStats last_stats_;
};

}  // namespace mulan::view
