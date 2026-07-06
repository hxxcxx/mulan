#include "render_scene.h"

#include <mulan/asset/asset_library.h>
#include <mulan/scene/components/bounds_component.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/render_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/entity_dirty.h>
#include <mulan/scene/scene.h>
#include <mulan/math/algo/intersect.h>

namespace mulan::view {

namespace {

/// 从 Scene 单个 entity 的组件派生一份 SceneProxy（不参与 bounds 累加）。
/// geometry 缺失时返回 std::nullopt（该 entity 不可渲染）。
/// worldBounds 由 asset 的 localBounds 经 world 矩阵变换重算 —— 这样 transform
/// 变化时 bounds 自动跟随（Scene 层只管 world 矩阵传播，不知 asset）。
std::optional<SceneProxy> buildProxy(const scene::Scene& scene, const asset::AssetLibrary& assets, scene::EntityId id) {
    const auto* geometry = scene.geometry(id);
    if (!geometry || !geometry->geometry)
        return std::nullopt;

    const asset::Asset* asset = assets.asset(geometry->geometry);
    if (!asset)
        return std::nullopt;

    // localBounds 仅 GeometryAsset 提供（其他资产类型 dynamic_cast 返回 nullptr）
    const auto* geomAsset = dynamic_cast<const asset::GeometryAsset*>(asset);
    if (!geomAsset)
        return std::nullopt;

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
    // worldBounds = localBounds(资产空间) × world 矩阵
    proxy.worldBounds = geomAsset->localBounds().transformed(proxy.worldTransform);
    return proxy;
}

}  // namespace

// ============================================================
// 全量重建（首次同步 / resetSync 后）
// ============================================================

void RenderScene::sync(scene::Scene& scene, const asset::AssetLibrary& assets) {
    if (!initialized_) {
        // 全量路径
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
        // 全量消费所有脏位
        scene.clearDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created |
                         scene::EntityDirty::Destroyed | scene::EntityDirty::Bounds);
        initialized_ = true;
        return;
    }

    // ── 增量路径：只处理自上次同步以来标脏的 entity ──
    last_sync_stats_ = {};
    last_sync_stats_.entityCount = scene.entityCount();
    last_sync_stats_.assetCount = assets.count();

    // 先处理销毁的 entity（proxy 移除）
    scene.forEachDirty(scene::EntityDirty::Destroyed,
                       [&](scene::EntityId id, uint64_t /*flags*/) { proxies_.erase(id); });

    // 处理创建/变更的 entity：重建该 proxy（局部字段更新等价于重建，代码统一）
    // 凡命中 RenderRelated / Created / Bounds 任一位的 entity 都重新派生 proxy。
    scene.forEachDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created | scene::EntityDirty::Bounds,
                       [&](scene::EntityId id, uint64_t flags) {
                           // 已销毁的跳过（Destroyed 已在上面处理，但同一 entity 可能同时带 Created+Destroyed
                           // 的极端时序；isValid 兜底）
                           if (!scene.isValid(id)) {
                               proxies_.erase(id);
                               return;
                           }

                           auto proxy = buildProxy(scene, assets, id);
                           if (!proxy) {
                               // 几何/资产缺失 → 移除已有 proxy（若有）
                               proxies_.erase(id);
                               ++last_sync_stats_.missingGeometryCount;
                               return;
                           }
                           proxies_[id] = std::move(*proxy);
                           (void) flags;
                       });

    // bounds 重新累加（AABB 无逆运算，删除/变更后整体重算 O(n)，远比 proxy map 重建便宜）
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

    // 清掉本次消费的脏位（Name 等非渲染相关位保留，不影响）
    scene.clearDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created | scene::EntityDirty::Destroyed |
                     scene::EntityDirty::Bounds);
}

void RenderScene::clear() {
    last_sync_stats_ = {};
    scene_bounds_.reset();
    proxies_.clear();
    initialized_ = false;
}

const SceneProxy* RenderScene::proxy(scene::EntityId id) const {
    auto it = proxies_.find(id);
    return it != proxies_.end() ? &it->second : nullptr;
}

std::optional<RenderScene::PickResult> RenderScene::pick(const math::Ray3& ray) const {
    std::optional<PickResult> best;
    for (const auto& [id, proxy] : proxies_) {
        if (!proxy.visible) {
            continue;
        }

        const auto hit = math::intersect(ray, proxy.worldBounds);
        if (!hit.hit) {
            continue;
        }

        if (!best || hit.t < best->distance) {
            best = PickResult{
                .entity = id,
                .pickId = proxy.entity.index(),
                .distance = hit.t,
            };
        }
    }
    return best;
}

}  // namespace mulan::view
