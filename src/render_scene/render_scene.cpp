#include "render_scene.h"

#include <mulan/asset/asset_library.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/render_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/scene.h>

namespace mulan::render_scene {

void RenderScene::sync(const scene::Scene& scene, const asset::AssetLibrary& assets) {
    proxies_.clear();
    last_sync_stats_ = {};
    last_sync_stats_.entityCount = scene.entityCount();
    last_sync_stats_.assetCount = assets.count();

    scene.forEachEntity([&](scene::EntityId id) {
        const auto* geometry = scene.geometry(id);
        const auto* render = scene.render(id);
        const auto* selection = scene.selection(id);
        const auto* transform = scene.transform(id);

        if (!geometry || !geometry->geometry) {
            ++last_sync_stats_.missingGeometryCount;
            return;
        }

        const asset::Asset* asset = assets.asset(geometry->geometry);
        if (!asset) {
            ++last_sync_stats_.missingGeometryCount;
            return;
        }

        SceneProxy proxy;
        proxy.entity = id;
        proxy.geometry = geometry->geometry;
        proxy.geometryKind = asset->kind();
        proxy.visible = render ? render->visible : true;
        proxy.selected = selection ? selection->selected : false;
        proxy.worldTransform = transform ? transform->world : engine::Mat4{1.0};

        if (proxy.visible)
            ++last_sync_stats_.visibleProxyCount;

        proxies_[id] = proxy;
    });

    last_sync_stats_.proxyCount = proxies_.size();
}

void RenderScene::clear() {
    last_sync_stats_ = {};
    proxies_.clear();
}

const SceneProxy* RenderScene::proxy(scene::EntityId id) const {
    auto it = proxies_.find(id);
    return it != proxies_.end() ? &it->second : nullptr;
}

} // namespace mulan::render_scene
