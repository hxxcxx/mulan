#include "render_scene.h"

#include <mulan/asset/asset_library.h>
#include <mulan/scene/scene.h>

namespace mulan::render_scene {

void RenderScene::sync(const scene::Scene& scene, const asset::AssetLibrary& assets) {
    last_sync_stats_.entityCount = scene.entityCount();
    last_sync_stats_.assetCount = assets.count();
}

void RenderScene::clear() {
    last_sync_stats_ = {};
}

} // namespace mulan::render_scene

