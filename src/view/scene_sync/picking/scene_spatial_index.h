/**
 * @file scene_spatial_index.h
 * @brief RenderScene 可见代理的增量空间索引适配层。
 * @author hxxcxx
 * @date 2026-07-16
 *
 * 通用层次结构由 math::DynamicBVH 提供。本类只负责场景 EntityId 适配、非法包围盒
 * 的保守 fallback 以及查询统计和稳定排序，不在视图层重复维护树算法。
 */
#pragma once

#include <mulan/math/spatial/dynamic_bvh.h>
#include <mulan/scene/entity_id.h>

#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace mulan::view::detail {

struct SpatialIndexEntry {
    scene::EntityId entity;
    math::AABB3 bounds;
};

struct SpatialIndexQueryStats {
    size_t indexedProxyCount = 0;
    size_t fallbackProxyCount = 0;
    size_t nodeBoundsTestCount = 0;
    size_t leafBoundsTestCount = 0;
    size_t candidateProxyCount = 0;
};

class SceneSpatialIndex {
public:
    static bool isIndexableBounds(const math::AABB3& bounds) {
        return math::DynamicBVH<scene::EntityId>::isValidBounds(bounds);
    }

    void rebuild(std::vector<SpatialIndexEntry> entries) {
        clear();
        // 构建结果不承诺固定树形，但固定插入顺序有利于复现实例级性能问题。
        std::sort(entries.begin(), entries.end(), [](const SpatialIndexEntry& lhs, const SpatialIndexEntry& rhs) {
            return lhs.entity.value < rhs.entity.value;
        });
        index_.reserve(entries.size());
        fallback_entities_.reserve(entries.size());
        for (const SpatialIndexEntry& entry : entries)
            upsert(entry.entity, entry.bounds);
    }

    void upsert(scene::EntityId entity, const math::AABB3& bounds) {
        fallback_entities_.erase(entity);
        if (!isIndexableBounds(bounds)) {
            index_.remove(entity);
            fallback_entities_.insert(entity);
            return;
        }

        if (index_.contains(entity)) {
            index_.update(entity, bounds);
        } else {
            index_.insert(entity, bounds);
        }
    }

    bool remove(scene::EntityId entity) {
        const bool removedFallback = fallback_entities_.erase(entity) != 0;
        const bool removedIndexed = index_.remove(entity);
        return removedFallback || removedIndexed;
    }

    void clear() {
        index_.clear();
        fallback_entities_.clear();
    }

    const math::AABB3& bounds() const { return index_.bounds(); }

    size_t indexedCount() const { return index_.size(); }
    size_t fallbackCount() const { return fallback_entities_.size(); }

    void queryRay(const math::Ray3& ray, double toleranceWorld, std::vector<scene::EntityId>& out,
                  SpatialIndexQueryStats* stats = nullptr) const {
        out.clear();
        SpatialIndexQueryStats localStats;
        localStats.indexedProxyCount = index_.size();
        localStats.fallbackProxyCount = fallback_entities_.size();

        math::DynamicBVH<scene::EntityId>::QueryStats treeStats;
        index_.queryRay(
                ray, toleranceWorld,
                [&out](scene::EntityId entity) {
                    out.push_back(entity);
                    return true;
                },
                &treeStats);
        localStats.nodeBoundsTestCount = treeStats.nodeBoundsTestCount;
        localStats.leafBoundsTestCount = treeStats.leafBoundsTestCount;

        // 非法 bounds 无法参与宽阶段，但不能因此漏掉潜在命中，继续交给精确查询。
        out.insert(out.end(), fallback_entities_.begin(), fallback_entities_.end());
        std::sort(out.begin(), out.end(),
                  [](scene::EntityId lhs, scene::EntityId rhs) { return lhs.value < rhs.value; });
        out.erase(std::unique(out.begin(), out.end()), out.end());
        localStats.candidateProxyCount = out.size();
        if (stats)
            *stats = localStats;
    }

    bool validate() const { return index_.validate(); }

private:
    math::DynamicBVH<scene::EntityId> index_;
    std::unordered_set<scene::EntityId> fallback_entities_;
};

}  // namespace mulan::view::detail
