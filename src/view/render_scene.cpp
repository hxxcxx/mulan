#include "render_scene.h"

#include "geometry_query.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/scene/components/bounds_component.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/light_component.h>
#include <mulan/scene/components/render_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/entity_dirty.h>
#include <mulan/scene/scene.h>

#include <optional>
#include <utility>
#include <vector>

namespace mulan::view {

namespace {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

engine::Light toRenderLight(const scene::LightComponent& src, const math::Mat4& world) {
    engine::Light dst;
    switch (src.kind) {
    case scene::LightKind::Directional: dst.type = engine::LightType::Directional; break;
    case scene::LightKind::Point: dst.type = engine::LightType::Point; break;
    case scene::LightKind::Spot: dst.type = engine::LightType::Spot; break;
    }

    dst.color = src.color;
    dst.intensity = src.intensity;
    dst.range = src.range;
    dst.innerConeAngle = src.innerConeAngle;
    dst.outerConeAngle = src.outerConeAngle;
    dst.position = math::Point3::origin().transformedBy(world).asVec();
    dst.direction = math::Vec3(0.0, 0.0, -1.0).transformedAsDir(world).normalizedOr(math::Vec3(-0.3, -1.0, -0.4));
    return dst;
}

void collectLights(const scene::Scene& scene, std::vector<engine::Light>& lights) {
    lights.clear();
    scene.forEachEntity([&](scene::EntityId id) {
        if (lights.size() >= engine::LightEnvironment::kMaxLights) {
            return;
        }

        const auto* light = scene.light(id);
        if (!light) {
            return;
        }

        const auto* transform = scene.transform(id);
        const math::Mat4 world = transform ? transform->world : math::Mat4{ 1.0 };
        lights.push_back(toRenderLight(*light, world));
    });
}

std::optional<SceneProxy> buildProxy(const scene::Scene& scene, const asset::AssetLibrary& assets, scene::EntityId id) {
    const auto* geometry = scene.geometry(id);
    if (!geometry || !geometry->geometry) {
        return std::nullopt;
    }

    const asset::Asset* asset = assets.asset(geometry->geometry);
    if (!asset) {
        return std::nullopt;
    }

    // localBounds is only provided by GeometryAsset.
    const auto* geomAsset = dynamic_cast<const asset::GeometryAsset*>(asset);
    if (!geomAsset) {
        return std::nullopt;
    }

    const auto* render = scene.render(id);
    const auto* selection = scene.selection(id);
    const auto* transform = scene.transform(id);

    SceneProxy proxy;
    proxy.entity = id;
    proxy.geometry = geometry->geometry;
    proxy.geometryKind = asset->kind();
    proxy.materialSlots = render ? render->material_slots : std::vector<asset::AssetId>{};
    proxy.visible = render ? render->visible : true;
    proxy.selected = selection ? selection->selected : false;
    proxy.worldTransform = transform ? transform->world : math::Mat4{ 1.0 };
    // worldBounds = local asset bounds transformed into world space.
    proxy.worldBounds = geomAsset->localBounds().transformed(proxy.worldTransform);
    return proxy;
}

}  // namespace

// ============================================================
// Full rebuild for the first sync or after resetSync().
// ============================================================

void RenderScene::sync(scene::Scene& scene, const asset::AssetLibrary& assets) {
    if (assets_ && assets_ != &assets) {
        initialized_ = false;
    }
    assets_ = &assets;
    collectLights(scene, lights_);

    if (!initialized_) {
        // Full sync path.
        proxies_.clear();
        last_sync_stats_ = {};
        scene_bounds_.reset();
        last_sync_stats_.entityCount = scene.entityCount();
        last_sync_stats_.assetCount = assets.count();

        scene.forEachEntity([&](scene::EntityId id) {
            auto proxy = buildProxy(scene, assets, id);
            if (!proxy) {
                ++last_sync_stats_.missingGeometryCount;
                return;
            }
            if (proxy->visible) {
                ++last_sync_stats_.visibleProxyCount;
                scene_bounds_.expand(proxy->worldBounds);
            }
            proxies_[id] = std::move(*proxy);
        });

        last_sync_stats_.proxyCount = proxies_.size();
        // Consume all render-related dirty flags after a full rebuild.
        scene.clearDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created |
                         scene::EntityDirty::Destroyed | scene::EntityDirty::Bounds);
        initialized_ = true;
        ++generation_;
        ++geometry_generation_;
        return;
    }

    // Incremental path: only process entities dirtied since the last sync.
    last_sync_stats_ = {};
    last_sync_stats_.entityCount = scene.entityCount();
    last_sync_stats_.assetCount = assets.count();

    bool changed = false;
    bool geometryChanged = false;

    // Remove destroyed proxies first.
    scene.forEachDirty(scene::EntityDirty::Destroyed, [&](scene::EntityId id, uint64_t /*flags*/) {
        proxies_.erase(id);
        changed = true;
        geometryChanged = true;
    });

    // Rebuild proxies for created or render-affecting entity changes.
    scene.forEachDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created | scene::EntityDirty::Bounds,
                       [&](scene::EntityId id, uint64_t flags) {
                           changed = true;
                           geometryChanged = geometryChanged ||
                                             scene::hasAnyDirty(flags, scene::EntityDirty::Geometry) ||
                                             scene::hasAnyDirty(flags, scene::EntityDirty::Created);
                           // A dirty entity can be destroyed and recreated in the same frame; validate before use.
                           if (!scene.isValid(id)) {
                               proxies_.erase(id);
                               return;
                           }

                           auto proxy = buildProxy(scene, assets, id);
                           if (!proxy) {
                               // Geometry or asset disappeared; remove any stale proxy.
                               proxies_.erase(id);
                               ++last_sync_stats_.missingGeometryCount;
                               return;
                           }
                           proxies_[id] = std::move(*proxy);
                           (void) flags;
                       });

    // Recompute scene bounds after removals or transform changes.
    scene_bounds_.reset();
    size_t visibleCount = 0;
    for (const auto& [id, proxy] : proxies_) {
        if (proxy.visible) {
            scene_bounds_.expand(proxy.worldBounds);
            ++visibleCount;
        }
    }
    last_sync_stats_.visibleProxyCount = visibleCount;
    last_sync_stats_.proxyCount = proxies_.size();

    // Clear only the dirty flags consumed by this render sync.
    scene.clearDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created | scene::EntityDirty::Destroyed |
                     scene::EntityDirty::Bounds);
    if (changed) {
        ++generation_;
    }
    if (geometryChanged) {
        ++geometry_generation_;
    }
}

void RenderScene::clear() {
    last_sync_stats_ = {};
    scene_bounds_.reset();
    proxies_.clear();
    lights_.clear();
    assets_ = nullptr;
    initialized_ = false;
    ++generation_;
    ++geometry_generation_;
}

const SceneProxy* RenderScene::proxy(scene::EntityId id) const {
    auto it = proxies_.find(id);
    return it != proxies_.end() ? &it->second : nullptr;
}

void RenderScene::collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld,
                                        std::vector<PickResult>& out) const {
    GeometryQueryWorld(*this).collectPickCandidates(ray, lineToleranceWorld, out);
}

std::optional<RenderScene::PickResult> RenderScene::pick(const math::Ray3& ray, double lineToleranceWorld) const {
    return GeometryQueryWorld(*this).pick(ray, lineToleranceWorld);
}

}  // namespace mulan::view
