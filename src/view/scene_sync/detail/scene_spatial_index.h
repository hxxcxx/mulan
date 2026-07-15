/**
 * @file scene_spatial_index.h
 * @brief RenderScene 可见代理的场景级三维 BVH 空间索引。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 本索引只承担保守的宽阶段裁剪：有限且非空的世界包围盒进入 BVH，空或
 * 非有限包围盒进入 fallback 列表并始终交给资产级精确拾取。查询容差会
 * 同时扩展内部节点和叶条目，避免线框容差命中被提前裁掉。
 *
 * 该文件是 scene_sync 内部实现，不属于公开 View API。实现保持 header-only，
 * 以免把内部类型加入库的公开安装面。
 */
#pragma once

#include <mulan/math/algo/intersect.h>
#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
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
        return !bounds.isEmpty() && std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) &&
               std::isfinite(bounds.min.z) && std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) &&
               std::isfinite(bounds.max.z);
    }

    /// 以当前可见代理快照重建索引。空或非有限 bounds 会被保守地放入 fallback。
    void rebuild(std::vector<SpatialIndexEntry> entries) {
        entries_.clear();
        fallback_entities_.clear();
        nodes_.clear();

        entries_.reserve(entries.size());
        fallback_entities_.reserve(entries.size());
        for (SpatialIndexEntry& entry : entries) {
            if (!isIndexableBounds(entry.bounds)) {
                fallback_entities_.push_back(entry.entity);
            } else {
                entries_.push_back(std::move(entry));
            }
        }

        const auto byEntity = [](scene::EntityId lhs, scene::EntityId rhs) {
            return lhs.value < rhs.value;
        };
        std::sort(fallback_entities_.begin(), fallback_entities_.end(), byEntity);
        std::sort(entries_.begin(), entries_.end(), [](const SpatialIndexEntry& lhs, const SpatialIndexEntry& rhs) {
            return lhs.entity.value < rhs.entity.value;
        });

        if (!entries_.empty()) {
            nodes_.reserve(entries_.size() * 2u);
            buildNode(0u, entries_.size());
        }
    }

    void clear() {
        entries_.clear();
        fallback_entities_.clear();
        nodes_.clear();
    }

    /// 返回宽阶段候选。结果按 EntityId 排序，便于诊断和确定性测试。
    void queryRay(const math::Ray3& ray, double toleranceWorld, std::vector<scene::EntityId>& out,
                  SpatialIndexQueryStats* stats = nullptr) const {
        out.clear();

        SpatialIndexQueryStats localStats;
        localStats.indexedProxyCount = entries_.size();
        localStats.fallbackProxyCount = fallback_entities_.size();
        const double tolerance = std::max(0.0, toleranceWorld);

        if (!nodes_.empty()) {
            std::vector<uint32_t> stack;
            stack.reserve(64u);
            stack.push_back(0u);
            while (!stack.empty()) {
                const uint32_t nodeIndex = stack.back();
                stack.pop_back();
                const Node& node = nodes_[nodeIndex];

                ++localStats.nodeBoundsTestCount;
                if (!math::intersect(ray, expanded(node.bounds, tolerance)).hit) {
                    continue;
                }

                if (node.isLeaf()) {
                    for (size_t i = node.first; i < node.first + node.count; ++i) {
                        ++localStats.leafBoundsTestCount;
                        const SpatialIndexEntry& entry = entries_[i];
                        if (math::intersect(ray, expanded(entry.bounds, tolerance)).hit) {
                            out.push_back(entry.entity);
                        }
                    }
                    continue;
                }

                // 后压入左节点，使遍历顺序在相同树结构下保持稳定。
                stack.push_back(node.right);
                stack.push_back(node.left);
            }
        }

        out.insert(out.end(), fallback_entities_.begin(), fallback_entities_.end());
        std::sort(out.begin(), out.end(),
                  [](scene::EntityId lhs, scene::EntityId rhs) { return lhs.value < rhs.value; });
        out.erase(std::unique(out.begin(), out.end()), out.end());
        localStats.candidateProxyCount = out.size();
        if (stats) {
            *stats = localStats;
        }
    }

private:
    static constexpr uint32_t kInvalidNode = std::numeric_limits<uint32_t>::max();
    static constexpr size_t kLeafCapacity = 4u;

    struct Node {
        math::AABB3 bounds;
        size_t first = 0;
        size_t count = 0;
        uint32_t left = kInvalidNode;
        uint32_t right = kInvalidNode;

        bool isLeaf() const { return count != 0; }
    };

    static math::AABB3 expanded(const math::AABB3& bounds, double amount) {
        if (amount <= 0.0 || bounds.isEmpty()) {
            return bounds;
        }
        math::AABB3 result = bounds;
        const math::Vec3 padding(amount, amount, amount);
        result.min -= padding;
        result.max += padding;
        return result;
    }

    uint32_t buildNode(size_t first, size_t count) {
        Node node;
        node.first = first;
        for (size_t i = first; i < first + count; ++i) {
            node.bounds.expand(entries_[i].bounds);
        }

        const uint32_t nodeIndex = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(node);
        if (count <= kLeafCapacity) {
            nodes_[nodeIndex].count = count;
            return nodeIndex;
        }

        math::AABB3 centroidBounds;
        for (size_t i = first; i < first + count; ++i) {
            centroidBounds.expand(entries_[i].bounds.center());
        }
        const math::Vec3 centroidSize = centroidBounds.size();
        int axis = 0;
        if (centroidSize.y > centroidSize.x) {
            axis = 1;
        }
        if (centroidSize.z > centroidSize[axis]) {
            axis = 2;
        }

        const size_t middle = first + count / 2u;
        std::nth_element(entries_.begin() + static_cast<std::ptrdiff_t>(first),
                         entries_.begin() + static_cast<std::ptrdiff_t>(middle),
                         entries_.begin() + static_cast<std::ptrdiff_t>(first + count),
                         [axis](const SpatialIndexEntry& lhs, const SpatialIndexEntry& rhs) {
                             const double lhsCenter = lhs.bounds.center()[axis];
                             const double rhsCenter = rhs.bounds.center()[axis];
                             if (lhsCenter != rhsCenter) {
                                 return lhsCenter < rhsCenter;
                             }
                             return lhs.entity.value < rhs.entity.value;
                         });

        const uint32_t left = buildNode(first, middle - first);
        const uint32_t right = buildNode(middle, first + count - middle);
        nodes_[nodeIndex].left = left;
        nodes_[nodeIndex].right = right;
        return nodeIndex;
    }

    std::vector<SpatialIndexEntry> entries_;
    std::vector<scene::EntityId> fallback_entities_;
    std::vector<Node> nodes_;
};

}  // namespace mulan::view::detail
