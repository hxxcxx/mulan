#include <mulan/view/scene_sync/render_scene.h>

#include "scene_sync/geometry_query.h"
#include "scene_sync/detail/primitive_pick_index.h"
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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mulan::view {

namespace {

uint64_t allocateRenderSceneChangeDomain() {
    static std::atomic<uint64_t> next{ 1 };
    uint64_t domain = next.fetch_add(1, std::memory_order_relaxed);
    if (domain == 0) {
        domain = next.fetch_add(1, std::memory_order_relaxed);
    }
    return domain;
}

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

math::Vec3 transformLightDirection(const math::Mat4& world) {
    const auto finiteVector = [](const math::Vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const math::Vec3 fallback =
            math::Vec3(0.0, 0.0, -1.0).transformedAsDir(world).normalizedOr(math::Vec3(0.0, 0.0, -1.0));

    math::Mat3 rotation(world);
    if (!finiteVector(rotation[0]) || !finiteVector(rotation[1]) || !finiteVector(rotation[2]))
        return fallback;
    const double scale = std::max({ rotation[0].length(), rotation[1].length(), rotation[2].length() });
    if (!std::isfinite(scale) || scale <= 1.0e-15)
        return fallback;
    rotation[0] /= scale;
    rotation[1] /= scale;
    rotation[2] /= scale;

    // 世界矩阵在父级非均匀缩放与子级旋转组合后会包含 shear。通过极分解迭代
    // 取得最接近的正交旋转，避免把缩放混入平行光和聚光灯方向。
    for (uint32_t iteration = 0; iteration < 12; ++iteration) {
        const double determinant = rotation.determinant();
        if (!std::isfinite(determinant) || determinant <= 1.0e-12)
            return fallback;

        const math::Mat3 inverseTranspose = rotation.inverse().transposed();
        const math::Mat3 next((rotation[0] + inverseTranspose[0]) * 0.5, (rotation[1] + inverseTranspose[1]) * 0.5,
                              (rotation[2] + inverseTranspose[2]) * 0.5);
        if (!finiteVector(next[0]) || !finiteVector(next[1]) || !finiteVector(next[2]))
            return fallback;

        const double delta = (next[0] - rotation[0]).lengthSq() + (next[1] - rotation[1]).lengthSq() +
                             (next[2] - rotation[2]).lengthSq();
        rotation = next;
        if (delta <= 1.0e-24)
            break;
    }

    return (rotation * math::Vec3(0.0, 0.0, -1.0)).normalizedOr(fallback);
}

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
    dst.direction = transformLightDirection(world);
    return dst.sanitized();
}

void selectLights(const std::unordered_map<scene::EntityId, engine::Light>& allLights,
                  std::vector<engine::Light>& lights) {
    struct Candidate {
        scene::EntityId entity;
        engine::Light light;
    };

    lights.clear();
    std::vector<Candidate> candidates;
    candidates.reserve(allLights.size());
    for (const auto& [entity, light] : allLights)
        candidates.push_back({ entity, light });

    // 固定灯位优先保留全局方向光，再按能量和 EntityId 排序；结果不依赖容器遍历顺序。
    const auto typeRank = [](engine::LightType type) {
        switch (type) {
        case engine::LightType::Directional: return 0;
        case engine::LightType::Spot: return 1;
        case engine::LightType::Point: return 2;
        }
        return 3;
    };
    std::ranges::sort(candidates, [&](const Candidate& lhs, const Candidate& rhs) {
        const int lhsRank = typeRank(lhs.light.type);
        const int rhsRank = typeRank(rhs.light.type);
        if (lhsRank != rhsRank)
            return lhsRank < rhsRank;
        if (lhs.light.intensity != rhs.light.intensity)
            return lhs.light.intensity > rhs.light.intensity;
        return lhs.entity.value < rhs.entity.value;
    });
    const size_t count = std::min(candidates.size(), static_cast<size_t>(engine::LightEnvironment::kMaxLights));
    lights.reserve(count);
    for (size_t index = 0; index < count; ++index)
        lights.push_back(candidates[index].light);
}

void collectLights(const scene::Scene& scene, std::unordered_map<scene::EntityId, engine::Light>& allLights,
                   std::vector<engine::Light>& lights) {
    allLights.clear();
    scene.forEachEntity([&](scene::EntityId id) {
        const auto* light = scene.light(id);
        if (!light) {
            return;
        }
        const auto* transform = scene.transform(id);
        const math::Mat4 world = transform ? transform->world : math::Mat4{ 1.0 };
        allLights.emplace(id, toRenderLight(*light, world));
    });
    selectLights(allLights, lights);
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
    proxy.localBounds = geomAsset->localBounds();
    proxy.worldBounds = proxy.localBounds.transformed(proxy.worldTransform);
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

bool sameMatrix(const math::Mat4& lhs, const math::Mat4& rhs) {
    for (int column = 0; column < 4; ++column) {
        if (lhs[column] != rhs[column]) {
            return false;
        }
    }
    return true;
}

bool sameSpatialProxy(const SceneProxy& lhs, const SceneProxy& rhs) {
    if (lhs.visible != rhs.visible) {
        return false;
    }
    return !lhs.visible || sameBounds(lhs.worldBounds, rhs.worldBounds);
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

RenderScene::RenderScene()
    : spatial_index_(std::make_unique<detail::SceneSpatialIndex>()),
      primitive_pick_indices_(std::make_unique<detail::PrimitivePickIndexCache>()),
      change_domain_(allocateRenderSceneChangeDomain()) {
}

RenderScene::~RenderScene() = default;

void RenderScene::trackMissingGeometry(scene::EntityId entity, asset::AssetId geometry) {
    if (!geometry) {
        untrackMissingGeometry(entity);
        return;
    }
    if (const auto current = missing_geometry_assets_.find(entity);
        current != missing_geometry_assets_.end() && current->second == geometry) {
        return;
    }

    untrackMissingGeometry(entity);
    missing_geometry_assets_.insert_or_assign(entity, geometry);
    missing_geometry_asset_users_[geometry].insert(entity);
}

void RenderScene::untrackMissingGeometry(scene::EntityId entity) {
    const auto current = missing_geometry_assets_.find(entity);
    if (current == missing_geometry_assets_.end()) {
        return;
    }
    const asset::AssetId geometry = current->second;
    missing_geometry_assets_.erase(current);
    const auto users = missing_geometry_asset_users_.find(geometry);
    if (users == missing_geometry_asset_users_.end()) {
        return;
    }
    users->second.erase(entity);
    if (users->second.empty()) {
        missing_geometry_asset_users_.erase(users);
    }
}

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
    scene_ = &scene;
    assets_ = &assets;
    primitive_pick_indices_->bindDomain(assets.domainId());

    const auto rebuildAll = [&]() {
        initialized_ = false;
        proxies_.clear();
        geometry_asset_revisions_.clear();
        geometry_asset_users_.clear();
        missing_geometry_assets_.clear();
        missing_geometry_asset_users_.clear();
        last_sync_stats_ = {};
        scene_bounds_.reset();
        last_sync_stats_.entityCount = scene.entityCount();
        last_sync_stats_.assetCount = assets.count();
        collectLights(scene, all_lights_, lights_);

        scene.forEachEntity([&](scene::EntityId id) {
            auto proxy = buildProxy(scene, assets, id);
            if (!proxy) {
                const auto* geometry = scene.geometry(id);
                if (geometry && geometry->geometry)
                    trackMissingGeometry(id, geometry->geometry);
                return;
            }
            proxy->pickId = pickIdForEntity(id);
            geometry_asset_revisions_[id] = geometryAssetRevision(assets, proxy->geometry);
            geometry_asset_users_[proxy->geometry].insert(id);
            proxies_[id] = std::move(*proxy);
        });

        for (auto it = entity_pick_ids_.begin(); it != entity_pick_ids_.end();) {
            if (!proxies_.contains(it->first))
                it = entity_pick_ids_.erase(it);
            else
                ++it;
        }

        last_sync_stats_.proxyCount = proxies_.size();
        last_sync_stats_.missingGeometryCount = missing_geometry_assets_.size();
        rebuildSpatialIndex();
        // 非有限 bounds 进入保守拾取 fallback，但不能污染相机使用的场景 bounds。
        // 全量与增量路径统一以 BVH 的有限根 bounds 为唯一聚合结果。
        scene_bounds_ = spatial_index_->bounds();
        last_sync_stats_.visibleProxyCount = spatial_index_->indexedCount() + spatial_index_->fallbackCount();
        scene_bounds_sphere_ = math::Sphere3::fromAABB(scene_bounds_);
        scene_change_cursor_ = scene.currentChangeCursor();
        asset_change_cursor_ = assets.currentChangeCursor();
        initialized_ = true;
        ++generation_;
        resetChangeJournal();
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
    const asset::AssetChangeSet assetChangeSet = assets.readChanges(asset_change_cursor_);
    if (assetChangeSet.requiresFullResync()) {
        primitive_pick_indices_->clear();
        rebuildAll();
        return;
    }
    if (std::ranges::any_of(assetChangeSet.changes, [](const asset::AssetChange& change) { return !change.asset; })) {
        primitive_pick_indices_->clear();
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
    for (const asset::AssetChange& change : assetChangeSet.changes) {
        primitive_pick_indices_->erase(change.asset);
        if (const auto users = geometry_asset_users_.find(change.asset); users != geometry_asset_users_.end()) {
            for (scene::EntityId entity : users->second) {
                entityChanges[entity] |= scene::dirtyValue(scene::EntityDirty::Geometry);
            }
        }
        // 缺失几何不在 proxies_ 中，必须通过反向等待索引精确唤醒；
        // 无关资产的创建/删除不能再迫使整个 RenderScene 重建。
        if (const auto waiters = missing_geometry_asset_users_.find(change.asset);
            waiters != missing_geometry_asset_users_.end()) {
            for (scene::EntityId entity : waiters->second) {
                entityChanges[entity] |= scene::dirtyValue(scene::EntityDirty::Geometry);
            }
        }
    }

    bool changed = false;
    bool geometryChanged = false;
    bool spatialChanged = false;
    bool lightChanged = false;
    std::unordered_map<scene::EntityId, RenderProxyDirty> changedEntities;
    std::unordered_set<scene::EntityId> spatialEntities;

    const auto markChanged = [&](scene::EntityId entity, RenderProxyDirty dirty) {
        if (dirty != RenderProxyDirty::None) {
            changedEntities[entity] |= dirty;
        }
    };

    const auto unlinkGeometryAsset = [&](scene::EntityId entity, asset::AssetId geometry) {
        auto users = geometry_asset_users_.find(geometry);
        if (users == geometry_asset_users_.end())
            return;
        users->second.erase(entity);
        if (users->second.empty())
            geometry_asset_users_.erase(users);
    };

    const scene::EntityDirty lightChangeMask = scene::EntityDirty::Created | scene::EntityDirty::Destroyed |
                                               scene::EntityDirty::Transform | scene::EntityDirty::Light;
    for (const auto& [id, flags] : entityChanges) {
        if (!scene::hasAnyDirty(flags, lightChangeMask)) {
            continue;
        }

        const bool wasLight = all_lights_.contains(id);
        const scene::LightComponent* light = scene.isValid(id) ? scene.light(id) : nullptr;
        const bool isLight = light != nullptr;
        if (wasLight || isLight) {
            lightChanged = true;
            if (isLight) {
                const auto* transform = scene.transform(id);
                const math::Mat4 world = transform ? transform->world : math::Mat4{ 1.0 };
                all_lights_.insert_or_assign(id, toRenderLight(*light, world));
            } else {
                all_lights_.erase(id);
            }
        }
    }
    if (lightChanged) {
        selectLights(all_lights_, lights_);
    }

    // Selection 由 ViewState 单独驱动，不属于 RenderWorld 变更；但 SceneProxy 的
    // 只读诊断字段仍保持当前值，避免增量路径与 journal 溢出后的全量结果不一致。
    for (const auto& [id, flags] : entityChanges) {
        if (!scene::hasAnyDirty(flags, scene::EntityDirty::Selection) || !scene.isValid(id)) {
            continue;
        }
        const auto proxy = proxies_.find(id);
        if (proxy == proxies_.end()) {
            continue;
        }
        const auto* selection = scene.selection(id);
        proxy->second.selected = selection ? selection->selected : false;
    }

    // 销毁记录保留发布时的完整 generation；先移除旧代理，再处理仍有效实体的最终状态。
    for (const auto& [id, flags] : entityChanges) {
        if (!scene::hasAnyDirty(flags, scene::EntityDirty::Destroyed)) {
            continue;
        }
        const auto old = proxies_.find(id);
        const bool removed = old != proxies_.end();
        if (removed) {
            unlinkGeometryAsset(id, old->second.geometry);
            proxies_.erase(old);
        }
        geometry_asset_revisions_.erase(id);
        entity_pick_ids_.erase(id);
        untrackMissingGeometry(id);
        if (removed) {
            changed = true;
            geometryChanged = true;
            spatialChanged = true;
            spatialEntities.insert(id);
            markChanged(id, RenderProxyDirty::Removed);
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
        const bool requiresFullProjection =
                scene::hasAnyDirty(flags, scene::EntityDirty::Created | scene::EntityDirty::Geometry);

        // 位姿、可见性和实体材质槽只依赖 SceneProxy 已缓存的数据。纯空间变化
        // 绝不能重新访问 GeometryAsset，否则相机拖动仍会退化为网格/材质全解析。
        if (!requiresFullProjection && oldProxy != proxies_.end()) {
            SceneProxy& proxy = oldProxy->second;
            const bool previousVisible = proxy.visible;
            const math::AABB3 previousBounds = proxy.worldBounds;
            RenderProxyDirty proxyDirty = RenderProxyDirty::None;

            if (scene::hasAnyDirty(flags, scene::EntityDirty::Transform)) {
                const auto* transform = scene.transform(id);
                const math::Mat4 world = transform ? transform->world : math::Mat4{ 1.0 };
                if (!sameMatrix(proxy.worldTransform, world)) {
                    proxy.worldTransform = world;
                    proxy.worldBounds = proxy.localBounds.transformed(proxy.worldTransform);
                    proxyDirty |= RenderProxyDirty::Placement;
                }
            }
            if (scene::hasAnyDirty(flags, scene::EntityDirty::RenderState)) {
                const auto* render = scene.render(id);
                const bool visible = render ? render->visible : true;
                if (proxy.visible != visible) {
                    proxy.visible = visible;
                    proxyDirty |= RenderProxyDirty::Visibility;
                }
            }
            if (scene::hasAnyDirty(flags, scene::EntityDirty::Material)) {
                const auto* render = scene.render(id);
                const std::vector<asset::AssetId> materialSlots =
                        render ? render->material_slots : std::vector<asset::AssetId>{};
                if (proxy.materialSlots != materialSlots) {
                    proxy.materialSlots = materialSlots;
                    proxyDirty |= RenderProxyDirty::Material;
                }
            }

            if (proxyDirty != RenderProxyDirty::None) {
                const bool proxySpatialChanged = previousVisible != proxy.visible ||
                                                 (proxy.visible && !sameBounds(previousBounds, proxy.worldBounds));
                changed = true;
                markChanged(id, proxyDirty);
                if (proxySpatialChanged) {
                    spatialChanged = true;
                    spatialEntities.insert(id);
                }
            }
            continue;
        }

        // 缺失几何上的纯 transform/visibility/material 变化不会使实体变得可投影；
        // 等待索引仍指向原资产，因此无需进行一次必然失败的资产解析。
        if (!requiresFullProjection && oldProxy == proxies_.end()) {
            continue;
        }

        auto proxy = buildProxy(scene, assets, id);
        if (!proxy) {
            const bool removed = oldProxy != proxies_.end();
            if (removed) {
                unlinkGeometryAsset(id, oldProxy->second.geometry);
                proxies_.erase(oldProxy);
            }
            geometry_asset_revisions_.erase(id);
            entity_pick_ids_.erase(id);
            untrackMissingGeometry(id);
            const auto* geometry = scene.geometry(id);
            if (geometry && geometry->geometry)
                trackMissingGeometry(id, geometry->geometry);
            if (removed) {
                changed = true;
                geometryChanged = true;
                spatialChanged = true;
                spatialEntities.insert(id);
                markChanged(id, RenderProxyDirty::Removed);
            }
            continue;
        }
        proxy->pickId = pickIdForEntity(id);
        untrackMissingGeometry(id);

        const uint64_t currentRevision = geometryAssetRevision(assets, proxy->geometry);
        const auto knownRevision = geometry_asset_revisions_.find(id);
        const bool assetRevisionChanged =
                knownRevision != geometry_asset_revisions_.end() && knownRevision->second != currentRevision;
        const bool newProxy = oldProxy == proxies_.end();
        const bool proxySpatialChanged = newProxy || !sameSpatialProxy(oldProxy->second, *proxy);
        RenderProxyDirty proxyDirty = RenderProxyDirty::None;
        if (newProxy) {
            proxyDirty = RenderProxyDirty::Added;
        } else {
            const SceneProxy& previous = oldProxy->second;
            if (assetRevisionChanged || previous.geometry != proxy->geometry ||
                previous.geometryKind != proxy->geometryKind || !sameBounds(previous.localBounds, proxy->localBounds)) {
                proxyDirty |= RenderProxyDirty::Geometry;
            }
            if (previous.materialSlots != proxy->materialSlots) {
                proxyDirty |= RenderProxyDirty::Material;
            }
            if (!sameMatrix(previous.worldTransform, proxy->worldTransform)) {
                proxyDirty |= RenderProxyDirty::Placement;
            }
            if (previous.visible != proxy->visible) {
                proxyDirty |= RenderProxyDirty::Visibility;
            }
        }

        changed = changed || proxyDirty != RenderProxyDirty::None;
        geometryChanged = geometryChanged ||
                          hasAnyRenderProxyDirty(proxyDirty, RenderProxyDirty::Added | RenderProxyDirty::Geometry);
        spatialChanged = spatialChanged || proxySpatialChanged;
        if (proxySpatialChanged)
            spatialEntities.insert(id);
        if (!newProxy && oldProxy->second.geometry != proxy->geometry)
            unlinkGeometryAsset(id, oldProxy->second.geometry);
        geometry_asset_users_[proxy->geometry].insert(id);
        geometry_asset_revisions_[id] = currentRevision;
        proxies_[id] = std::move(*proxy);
        markChanged(id, proxyDirty);
    }

    // 动态空间索引只更新真正改变的实体；根节点缓存的 subtree bounds 同时给出
    // 场景总 bounds，不再为一次变换重新扫描全部代理或重建整棵树。
    if (spatialChanged) {
        for (scene::EntityId id : spatialEntities) {
            spatial_index_->remove(id);
            const auto proxy = proxies_.find(id);
            if (proxy != proxies_.end() && proxy->second.visible) {
                spatial_index_->upsert(id, proxy->second.worldBounds);
            }
        }
        scene_bounds_ = spatial_index_->bounds();
        last_sync_stats_.visibleProxyCount = spatial_index_->indexedCount() + spatial_index_->fallbackCount();
        scene_bounds_sphere_ = math::Sphere3::fromAABB(scene_bounds_);
    } else {
        last_sync_stats_.visibleProxyCount = previousVisibleProxyCount;
    }
    last_sync_stats_.proxyCount = proxies_.size();
    last_sync_stats_.missingGeometryCount = missing_geometry_assets_.size();

    if (changed) {
        ++generation_;
    }
    if (geometryChanged) {}
    appendChanges(changedEntities);
    // 只有上述投影更新全部成功后才确认 journal 进度；若中途抛出，下一次 sync
    // 仍会从旧 cursor 重放，避免“游标已前进、派生状态未更新”的永久漏变更。
    scene_change_cursor_ = changeSet.cursorAfterApply();
    asset_change_cursor_ = assetChangeSet.cursorAfterApply();
    syncGuard.commit();
}

void RenderScene::clear() {
    last_sync_stats_ = {};
    scene_bounds_.reset();
    scene_bounds_sphere_.reset();
    proxies_.clear();
    geometry_asset_revisions_.clear();
    geometry_asset_users_.clear();
    entity_pick_ids_.clear();
    missing_geometry_assets_.clear();
    missing_geometry_asset_users_.clear();
    lights_.clear();
    all_lights_.clear();
    spatial_index_->clear();
    primitive_pick_indices_->clear();
    scene_ = nullptr;
    assets_ = nullptr;
    scene_change_cursor_ = {};
    asset_change_cursor_ = {};
    initialized_ = false;
    ++generation_;
    resetChangeJournal();
}

void RenderScene::resetChangeJournal() {
    change_journal_.clear();
    change_domain_ = allocateRenderSceneChangeDomain();
    change_revision_ = 1;
}

void RenderScene::appendChanges(const std::unordered_map<scene::EntityId, RenderProxyDirty>& entities) {
    if (entities.empty()) {
        return;
    }
    ++change_revision_;
    if (change_revision_ == 0) {
        resetChangeJournal();
        return;
    }
    constexpr size_t MaxJournalEntries = 4096;
    std::vector<std::pair<scene::EntityId, RenderProxyDirty>> orderedChanges(entities.begin(), entities.end());
    std::ranges::sort(orderedChanges,
                      [](const auto& lhs, const auto& rhs) { return lhs.first.value < rhs.first.value; });
    for (const auto& [entity, dirty] : orderedChanges) {
        change_journal_.push_back(RenderSceneChange{ change_revision_, entity, dirty });
    }
    while (change_journal_.size() > MaxJournalEntries) {
        // revision 是一次 RenderScene 同步的原子批次；容量不足时必须整批丢弃，
        // 否则消费者可能只读到同一 revision 的后半段而无法发现漏变更。
        const uint64_t discardedRevision = change_journal_.front().revision;
        while (!change_journal_.empty() && change_journal_.front().revision == discardedRevision) {
            change_journal_.pop_front();
        }
    }
}

RenderSceneChangeSet RenderScene::readChanges(const RenderSceneChangeCursor& cursor) const {
    RenderSceneChangeSet result;
    result.domain = change_domain_;
    result.toRevision = change_revision_;
    if (cursor.domain != change_domain_ || cursor.revision > change_revision_) {
        result.status = RenderSceneChangeStatus::FullResyncRequired;
        return result;
    }
    if (cursor.revision == change_revision_) {
        return result;
    }
    if (change_journal_.empty() || cursor.revision < change_journal_.front().revision - 1) {
        result.status = RenderSceneChangeStatus::FullResyncRequired;
        return result;
    }
    result.status = RenderSceneChangeStatus::Changes;
    for (const RenderSceneChange& change : change_journal_) {
        if (change.revision > cursor.revision) {
            result.changes.push_back(change);
        }
    }
    return result;
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
