#include <mulan/view/scene_sync/render_scene.h>

#include "scene_sync/geometry_query.h"
#include "scene_sync/detail/scene_spatial_index.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/light_component.h>
#include <mulan/scene/components/render_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/entity_dirty.h>
#include <mulan/scene/scene.h>

#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
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

void collectLights(const scene::Scene& scene, std::vector<engine::Light>& lights,
                   std::unordered_set<scene::EntityId>& lightEntities) {
    lights.clear();
    lightEntities.clear();
    scene.forEachEntity([&](scene::EntityId id) {
        const auto* light = scene.light(id);
        if (!light) {
            return;
        }
        lightEntities.insert(id);
        if (lights.size() >= engine::LightEnvironment::kMaxLights) {
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

uint64_t geometryAssetRevision(const asset::AssetLibrary& assets, asset::AssetId id) {
    return assets.contentRevision(id).value_or(0);
}

bool sameBounds(const math::AABB3& lhs, const math::AABB3& rhs) {
    const bool lhsEmpty = lhs.isEmpty();
    const bool rhsEmpty = rhs.isEmpty();
    if (lhsEmpty || rhsEmpty) {
        return lhsEmpty == rhsEmpty;
    }
    return lhs.min == rhs.min && lhs.max == rhs.max;
}

bool sameSpatialProxy(const SceneProxy& lhs, const SceneProxy& rhs) {
    if (lhs.visible != rhs.visible) {
        return false;
    }
    return !lhs.visible || sameBounds(lhs.worldBounds, rhs.worldBounds);
}

bool hasGeometryReference(const scene::Scene& scene, scene::EntityId id) {
    const auto* geometry = scene.geometry(id);
    return geometry && static_cast<bool>(geometry->geometry);
}

/// 增量投影原地更新时的失败恢复护栏。任何异常都会让下一次 sync 走全量，
/// 从而修复可能已经部分更新的 proxy/light/BVH 状态。
class IncrementalSyncGuard {
public:
    explicit IncrementalSyncGuard(bool& initialized) : initialized_(initialized) {}
    ~IncrementalSyncGuard() {
        if (!committed_)
            initialized_ = false;
    }

    void commit() { committed_ = true; }

private:
    bool& initialized_;
    bool committed_ = false;
};

}  // namespace

RenderScene::RenderScene() : spatial_index_(std::make_unique<detail::SceneSpatialIndex>()) {
}

RenderScene::~RenderScene() = default;

// ============================================================
// Full rebuild for the first sync or after resetSync().
// ============================================================

void RenderScene::sync(const scene::Scene& scene, const asset::AssetLibrary& assets) {
    // 地址不足以唯一标识 Scene 生命周期：对象可能在同一存储上析构后重建。
    // journal domain 随 Scene 实例变化，可阻止这种同址换源复用旧 PickId。
    const bool sceneDomainChanged =
            scene_change_cursor_.domain != 0 && scene_change_cursor_.domain != scene.changeDomain();
    const bool sceneSourceChanged = scene_ != &scene || sceneDomainChanged;
    if (sceneSourceChanged || assets_ != &assets) {
        initialized_ = false;
    }
    if (sceneSourceChanged) {
        // Scene 身份切换后不能复用旧实体的 32-bit PickId；分配计数保持单调，
        // 避免旧异步帧/hover 与新 Scene 的首批实体发生 ABA。
        entity_pick_ids_.clear();
    }
    const uint64_t assetsMembershipRevision = assets.membershipRevision();
    if (initialized_ && assets_membership_revision_ != assetsMembershipRevision) {
        // 集合变化可能让原先缺失的几何首次出现；此类实体不在 proxies_ 中，
        // 无法仅扫描现有代理发现，因此走确定性的全量恢复。
        initialized_ = false;
    }
    scene_ = &scene;
    assets_ = &assets;

    const auto rebuildAll = [&]() {
        initialized_ = false;
        proxies_.clear();
        geometry_asset_revisions_.clear();
        missing_geometry_entities_.clear();
        last_sync_stats_ = {};
        scene_bounds_.reset();
        last_sync_stats_.entityCount = scene.entityCount();
        last_sync_stats_.assetCount = assets.count();
        collectLights(scene, lights_, light_entities_);

        scene.forEachEntity([&](scene::EntityId id) {
            auto proxy = buildProxy(scene, assets, id);
            if (!proxy) {
                if (hasGeometryReference(scene, id))
                    missing_geometry_entities_.insert(id);
                return;
            }
            proxy->pickId = pickIdForEntity(id);
            if (proxy->visible) {
                ++last_sync_stats_.visibleProxyCount;
                scene_bounds_.expand(proxy->worldBounds);
            }
            geometry_asset_revisions_[id] = geometryAssetRevision(assets, proxy->geometry);
            proxies_[id] = std::move(*proxy);
        });

        for (auto it = entity_pick_ids_.begin(); it != entity_pick_ids_.end();) {
            if (!proxies_.contains(it->first))
                it = entity_pick_ids_.erase(it);
            else
                ++it;
        }

        last_sync_stats_.proxyCount = proxies_.size();
        last_sync_stats_.missingGeometryCount = missing_geometry_entities_.size();
        scene_bounds_sphere_ = math::Sphere3::fromAABB(scene_bounds_);
        rebuildSpatialIndex();
        scene_change_cursor_ = scene.currentChangeCursor();
        assets_membership_revision_ = assetsMembershipRevision;
        initialized_ = true;
        ++generation_;
        ++geometry_generation_;
    };

    if (!initialized_) {
        rebuildAll();
        return;
    }

    const scene::SceneChangeSet changeSet = scene.readChanges(scene_change_cursor_);
    if (changeSet.requiresFullResync()) {
        rebuildAll();
        return;
    }

    IncrementalSyncGuard syncGuard(initialized_);
    const size_t previousVisibleProxyCount = last_sync_stats_.visibleProxyCount;
    last_sync_stats_ = {};
    last_sync_stats_.entityCount = scene.entityCount();
    last_sync_stats_.assetCount = assets.count();

    std::unordered_map<scene::EntityId, uint64_t> entityChanges;
    entityChanges.reserve(changeSet.changes.size());
    for (const scene::SceneChange& change : changeSet.changes) {
        entityChanges[change.entity] |= scene::dirtyValue(change.dirty);
    }

    bool changed = false;
    bool geometryChanged = false;
    bool spatialChanged = false;
    bool lightChanged = false;

    const scene::EntityDirty lightChangeMask = scene::EntityDirty::Created | scene::EntityDirty::Destroyed |
                                               scene::EntityDirty::Transform | scene::EntityDirty::Light;
    for (const auto& [id, flags] : entityChanges) {
        if (!scene::hasAnyDirty(flags, lightChangeMask)) {
            continue;
        }

        const bool wasLight = light_entities_.contains(id);
        const bool isLight = scene.isValid(id) && scene.light(id) != nullptr;
        if (wasLight || isLight) {
            lightChanged = true;
        }
    }
    if (lightChanged) {
        collectLights(scene, lights_, light_entities_);
    }

    // 销毁记录保留发布时的完整 generation；先移除旧代理，再处理仍有效实体的最终状态。
    for (const auto& [id, flags] : entityChanges) {
        if (!scene::hasAnyDirty(flags, scene::EntityDirty::Destroyed)) {
            continue;
        }
        const bool removed = proxies_.erase(id) > 0;
        geometry_asset_revisions_.erase(id);
        entity_pick_ids_.erase(id);
        missing_geometry_entities_.erase(id);
        if (removed) {
            changed = true;
            geometryChanged = true;
            spatialChanged = true;
        }
    }

    const scene::EntityDirty proxyChangeMask = scene::EntityDirty::Created | scene::EntityDirty::Transform |
                                               scene::EntityDirty::Geometry | scene::EntityDirty::RenderState |
                                               scene::EntityDirty::Material;
    for (const auto& [id, flags] : entityChanges) {
        if (scene::hasAnyDirty(flags, scene::EntityDirty::Destroyed) || !scene::hasAnyDirty(flags, proxyChangeMask)) {
            continue;
        }
        if (!scene.isValid(id)) {
            continue;
        }

        const auto oldProxy = proxies_.find(id);
        auto proxy = buildProxy(scene, assets, id);
        if (!proxy) {
            const bool removed = proxies_.erase(id) > 0;
            geometry_asset_revisions_.erase(id);
            entity_pick_ids_.erase(id);
            if (hasGeometryReference(scene, id))
                missing_geometry_entities_.insert(id);
            else
                missing_geometry_entities_.erase(id);
            if (removed) {
                changed = true;
                geometryChanged = true;
                spatialChanged = true;
            }
            continue;
        }
        proxy->pickId = pickIdForEntity(id);
        missing_geometry_entities_.erase(id);

        const uint64_t currentRevision = geometryAssetRevision(assets, proxy->geometry);
        const auto knownRevision = geometry_asset_revisions_.find(id);
        const bool assetRevisionChanged =
                knownRevision != geometry_asset_revisions_.end() && knownRevision->second != currentRevision;
        const bool newProxy = oldProxy == proxies_.end();
        const bool proxySpatialChanged = newProxy || !sameSpatialProxy(oldProxy->second, *proxy);

        changed = true;
        geometryChanged = geometryChanged || newProxy || assetRevisionChanged ||
                          scene::hasAnyDirty(flags, scene::EntityDirty::Created | scene::EntityDirty::Geometry);
        spatialChanged = spatialChanged || proxySpatialChanged;
        geometry_asset_revisions_[id] = currentRevision;
        proxies_[id] = std::move(*proxy);
    }

    // Scene journal 记录实体关系；资产对象内部的原地修改由逐资产 revision 补齐。
    // Material/Texture revision 由 RenderWorldSync 观察，不会误触碰场景空间索引。
    std::vector<scene::EntityId> revisionChanged;
    revisionChanged.reserve(proxies_.size());
    for (const auto& [id, proxy] : proxies_) {
        const uint64_t currentRevision = geometryAssetRevision(assets, proxy.geometry);
        const auto known = geometry_asset_revisions_.find(id);
        if (known == geometry_asset_revisions_.end() || known->second != currentRevision) {
            revisionChanged.push_back(id);
        }
    }
    for (scene::EntityId id : revisionChanged) {
        const auto oldProxy = proxies_.find(id);
        auto proxy = buildProxy(scene, assets, id);
        if (!proxy) {
            const bool removed = proxies_.erase(id) > 0;
            geometry_asset_revisions_.erase(id);
            entity_pick_ids_.erase(id);
            if (hasGeometryReference(scene, id))
                missing_geometry_entities_.insert(id);
            else
                missing_geometry_entities_.erase(id);
            spatialChanged = spatialChanged || removed;
        } else {
            proxy->pickId = pickIdForEntity(id);
            missing_geometry_entities_.erase(id);
            spatialChanged =
                    spatialChanged || oldProxy == proxies_.end() || !sameSpatialProxy(oldProxy->second, *proxy);
            geometry_asset_revisions_[id] = geometryAssetRevision(assets, proxy->geometry);
            proxies_[id] = std::move(*proxy);
        }
        changed = true;
        geometryChanged = true;
    }

    // 仅空间内容变化时重算全局 bounds/BVH。Selection、Material 等非空间修改
    // 复用上次结果，避免每次交互都线性扫描全部代理。
    if (spatialChanged) {
        scene_bounds_.reset();
        size_t visibleCount = 0;
        for (const auto& [id, proxy] : proxies_) {
            if (proxy.visible) {
                scene_bounds_.expand(proxy.worldBounds);
                ++visibleCount;
            }
        }
        last_sync_stats_.visibleProxyCount = visibleCount;
        scene_bounds_sphere_ = math::Sphere3::fromAABB(scene_bounds_);
        rebuildSpatialIndex();
    } else {
        last_sync_stats_.visibleProxyCount = previousVisibleProxyCount;
    }
    last_sync_stats_.proxyCount = proxies_.size();
    last_sync_stats_.missingGeometryCount = missing_geometry_entities_.size();

    if (changed) {
        ++generation_;
    }
    if (geometryChanged) {
        ++geometry_generation_;
    }
    // 只有上述投影更新全部成功后才确认 journal 进度；若中途抛出，下一次 sync
    // 仍会从旧 cursor 重放，避免“游标已前进、派生状态未更新”的永久漏变更。
    scene_change_cursor_ = changeSet.cursorAfterApply();
    syncGuard.commit();
}

void RenderScene::clear() {
    last_sync_stats_ = {};
    scene_bounds_.reset();
    scene_bounds_sphere_.reset();
    proxies_.clear();
    geometry_asset_revisions_.clear();
    entity_pick_ids_.clear();
    missing_geometry_entities_.clear();
    lights_.clear();
    light_entities_.clear();
    spatial_index_->clear();
    scene_ = nullptr;
    assets_ = nullptr;
    scene_change_cursor_ = {};
    assets_membership_revision_ = 0;
    initialized_ = false;
    ++generation_;
    ++geometry_generation_;
}

const SceneProxy* RenderScene::proxy(scene::EntityId id) const {
    auto it = proxies_.find(id);
    return it != proxies_.end() ? &it->second : nullptr;
}

engine::PickId RenderScene::pickIdForEntity(scene::EntityId entity) {
    if (const auto found = entity_pick_ids_.find(entity); found != entity_pick_ids_.end())
        return found->second;
    if (next_pick_id_ > std::numeric_limits<uint32_t>::max())
        throw std::overflow_error("RenderScene pick id exhausted");

    const engine::PickId pickId = engine::PickId::fromValue(static_cast<uint32_t>(next_pick_id_++));
    entity_pick_ids_.emplace(entity, pickId);
    return pickId;
}

void RenderScene::rebuildSpatialIndex() {
    std::vector<detail::SpatialIndexEntry> entries;
    entries.reserve(proxies_.size());
    for (const auto& [id, proxy] : proxies_) {
        if (proxy.visible) {
            entries.push_back(detail::SpatialIndexEntry{ id, proxy.worldBounds });
        }
    }
    spatial_index_->rebuild(std::move(entries));
}

void RenderScene::collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld, std::vector<PickResult>& out,
                                        PickQueryStats* stats) const {
    GeometryQueryWorld(*this).collectPickCandidates(ray, lineToleranceWorld, out, stats);
}

std::optional<RenderScene::PickResult> RenderScene::pick(const math::Ray3& ray, double lineToleranceWorld,
                                                         PickQueryStats* stats) const {
    return GeometryQueryWorld(*this).pick(ray, lineToleranceWorld, stats);
}

}  // namespace mulan::view
