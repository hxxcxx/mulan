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

#include <algorithm>
#include <atomic>
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

RenderScene::RenderScene()
    : spatial_index_(std::make_unique<detail::SceneSpatialIndex>()), change_domain_(allocateRenderSceneChangeDomain()) {
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
        geometry_asset_users_.clear();
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
        last_sync_stats_.missingGeometryCount = missing_geometry_entities_.size();
        scene_bounds_sphere_ = math::Sphere3::fromAABB(scene_bounds_);
        rebuildSpatialIndex();
        scene_change_cursor_ = scene.currentChangeCursor();
        asset_change_cursor_ = assets.currentChangeCursor();
        assets_membership_revision_ = assetsMembershipRevision;
        initialized_ = true;
        ++generation_;
        ++geometry_generation_;
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
        rebuildAll();
        return;
    }
    if (std::ranges::any_of(assetChangeSet.changes, [](const asset::AssetChange& change) { return !change.asset; })) {
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
        const auto users = geometry_asset_users_.find(change.asset);
        if (users == geometry_asset_users_.end()) {
            continue;
        }
        for (scene::EntityId entity : users->second) {
            entityChanges[entity] |= scene::dirtyValue(scene::EntityDirty::Geometry);
        }
    }

    bool changed = false;
    bool geometryChanged = false;
    bool spatialChanged = false;
    bool lightChanged = false;
    std::unordered_set<scene::EntityId> changedEntities;
    std::unordered_set<scene::EntityId> spatialEntities;

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
        const auto old = proxies_.find(id);
        const bool removed = old != proxies_.end();
        if (removed) {
            unlinkGeometryAsset(id, old->second.geometry);
            proxies_.erase(old);
        }
        geometry_asset_revisions_.erase(id);
        entity_pick_ids_.erase(id);
        missing_geometry_entities_.erase(id);
        if (removed) {
            changed = true;
            geometryChanged = true;
            spatialChanged = true;
            spatialEntities.insert(id);
            changedEntities.insert(id);
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
            const bool removed = oldProxy != proxies_.end();
            if (removed) {
                unlinkGeometryAsset(id, oldProxy->second.geometry);
                proxies_.erase(oldProxy);
            }
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
                spatialEntities.insert(id);
                changedEntities.insert(id);
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
        if (proxySpatialChanged)
            spatialEntities.insert(id);
        if (!newProxy && oldProxy->second.geometry != proxy->geometry)
            unlinkGeometryAsset(id, oldProxy->second.geometry);
        geometry_asset_users_[proxy->geometry].insert(id);
        geometry_asset_revisions_[id] = currentRevision;
        proxies_[id] = std::move(*proxy);
        changedEntities.insert(id);
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
    last_sync_stats_.missingGeometryCount = missing_geometry_entities_.size();

    if (changed) {
        ++generation_;
    }
    if (geometryChanged) {
        ++geometry_generation_;
    }
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
    missing_geometry_entities_.clear();
    lights_.clear();
    light_entities_.clear();
    spatial_index_->clear();
    scene_ = nullptr;
    assets_ = nullptr;
    scene_change_cursor_ = {};
    asset_change_cursor_ = {};
    assets_membership_revision_ = 0;
    initialized_ = false;
    ++generation_;
    ++geometry_generation_;
    resetChangeJournal();
}

void RenderScene::resetChangeJournal() {
    change_journal_.clear();
    change_domain_ = allocateRenderSceneChangeDomain();
    change_revision_ = 1;
}

void RenderScene::appendChanges(const std::unordered_set<scene::EntityId>& entities) {
    if (entities.empty()) {
        return;
    }
    ++change_revision_;
    if (change_revision_ == 0) {
        resetChangeJournal();
        return;
    }
    constexpr size_t MaxJournalEntries = 4096;
    for (scene::EntityId entity : entities) {
        change_journal_.push_back(RenderSceneChange{ change_revision_, entity });
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
