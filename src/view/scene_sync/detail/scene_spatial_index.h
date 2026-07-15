/**
 * @file scene_spatial_index.h
 * @brief RenderScene 可见代理的可增量更新空间索引。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 索引采用按包围盒中心排序的确定性 Treap。每个节点缓存整棵子树的 AABB，单实体
 * 插入、删除和移动只更新一条期望 O(log N) 路径，根节点同时给出场景总 bounds。
 * 空或非有限包围盒进入 fallback 集合，继续交给资产级精确拾取，保证宽阶段保守。
 */

#pragma once

#include <mulan/math/algo/intersect.h>
#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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

    void rebuild(std::vector<SpatialIndexEntry> entries) {
        clear();
        for (SpatialIndexEntry& entry : entries) {
            upsert(entry.entity, entry.bounds);
        }
    }

    void upsert(scene::EntityId entity, const math::AABB3& bounds) {
        remove(entity);
        if (!isIndexableBounds(bounds)) {
            fallback_entities_.insert(entity);
            return;
        }
        const Key key = makeKey(entity, bounds);
        keys_.emplace(entity, key);
        root_ = insert(std::move(root_), std::make_unique<Node>(entity, bounds, key));
    }

    bool remove(scene::EntityId entity) {
        if (fallback_entities_.erase(entity) != 0) {
            return true;
        }
        const auto known = keys_.find(entity);
        if (known == keys_.end()) {
            return false;
        }
        root_ = erase(std::move(root_), known->second);
        keys_.erase(known);
        return true;
    }

    void clear() {
        root_.reset();
        keys_.clear();
        fallback_entities_.clear();
    }

    const math::AABB3& bounds() const {
        static const math::AABB3 empty;
        return root_ ? root_->subtreeBounds : empty;
    }

    size_t indexedCount() const { return keys_.size(); }
    size_t fallbackCount() const { return fallback_entities_.size(); }

    void queryRay(const math::Ray3& ray, double toleranceWorld, std::vector<scene::EntityId>& out,
                  SpatialIndexQueryStats* stats = nullptr) const {
        out.clear();
        SpatialIndexQueryStats localStats;
        localStats.indexedProxyCount = keys_.size();
        localStats.fallbackProxyCount = fallback_entities_.size();
        const double tolerance = std::max(0.0, toleranceWorld);

        if (root_) {
            std::vector<const Node*> stack{ root_.get() };
            while (!stack.empty()) {
                const Node* node = stack.back();
                stack.pop_back();
                ++localStats.nodeBoundsTestCount;
                if (!math::intersect(ray, expanded(node->subtreeBounds, tolerance)).hit) {
                    continue;
                }
                ++localStats.leafBoundsTestCount;
                if (math::intersect(ray, expanded(node->entry.bounds, tolerance)).hit) {
                    out.push_back(node->entry.entity);
                }
                if (node->right)
                    stack.push_back(node->right.get());
                if (node->left)
                    stack.push_back(node->left.get());
            }
        }

        out.insert(out.end(), fallback_entities_.begin(), fallback_entities_.end());
        std::sort(out.begin(), out.end(),
                  [](scene::EntityId lhs, scene::EntityId rhs) { return lhs.value < rhs.value; });
        out.erase(std::unique(out.begin(), out.end()), out.end());
        localStats.candidateProxyCount = out.size();
        if (stats)
            *stats = localStats;
    }

private:
    struct Key {
        math::Point3 center;
        uint64_t entity = 0;
    };

    struct Node {
        Node(scene::EntityId entity, math::AABB3 bounds, Key nodeKey)
            : entry{ entity, std::move(bounds) },
              key(nodeKey),
              priority(priorityFor(entity)),
              subtreeBounds(entry.bounds) {}

        SpatialIndexEntry entry;
        Key key;
        uint64_t priority = 0;
        math::AABB3 subtreeBounds;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
    };

    static Key makeKey(scene::EntityId entity, const math::AABB3& bounds) { return { bounds.center(), entity.value }; }

    static bool less(const Key& lhs, const Key& rhs) {
        if (lhs.center.x != rhs.center.x)
            return lhs.center.x < rhs.center.x;
        if (lhs.center.y != rhs.center.y)
            return lhs.center.y < rhs.center.y;
        if (lhs.center.z != rhs.center.z)
            return lhs.center.z < rhs.center.z;
        return lhs.entity < rhs.entity;
    }

    static uint64_t priorityFor(scene::EntityId entity) {
        uint64_t value = entity.value + 0x9e3779b97f4a7c15ull;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31u);
    }

    static void refresh(Node& node) {
        node.subtreeBounds = node.entry.bounds;
        if (node.left)
            node.subtreeBounds.expand(node.left->subtreeBounds);
        if (node.right)
            node.subtreeBounds.expand(node.right->subtreeBounds);
    }

    static std::unique_ptr<Node> rotateRight(std::unique_ptr<Node> root) {
        auto next = std::move(root->left);
        root->left = std::move(next->right);
        refresh(*root);
        next->right = std::move(root);
        refresh(*next);
        return next;
    }

    static std::unique_ptr<Node> rotateLeft(std::unique_ptr<Node> root) {
        auto next = std::move(root->right);
        root->right = std::move(next->left);
        refresh(*root);
        next->left = std::move(root);
        refresh(*next);
        return next;
    }

    static std::unique_ptr<Node> insert(std::unique_ptr<Node> root, std::unique_ptr<Node> node) {
        if (!root)
            return node;
        if (less(node->key, root->key)) {
            root->left = insert(std::move(root->left), std::move(node));
            if (root->left->priority < root->priority)
                root = rotateRight(std::move(root));
        } else {
            root->right = insert(std::move(root->right), std::move(node));
            if (root->right->priority < root->priority)
                root = rotateLeft(std::move(root));
        }
        refresh(*root);
        return root;
    }

    static std::unique_ptr<Node> erase(std::unique_ptr<Node> root, const Key& key) {
        if (!root)
            return nullptr;
        if (less(key, root->key)) {
            root->left = erase(std::move(root->left), key);
        } else if (less(root->key, key)) {
            root->right = erase(std::move(root->right), key);
        } else if (!root->left) {
            return std::move(root->right);
        } else if (!root->right) {
            return std::move(root->left);
        } else if (root->left->priority < root->right->priority) {
            root = rotateRight(std::move(root));
            root->right = erase(std::move(root->right), key);
        } else {
            root = rotateLeft(std::move(root));
            root->left = erase(std::move(root->left), key);
        }
        refresh(*root);
        return root;
    }

    static math::AABB3 expanded(const math::AABB3& bounds, double amount) {
        if (amount <= 0.0 || bounds.isEmpty())
            return bounds;
        math::AABB3 result = bounds;
        const math::Vec3 padding(amount, amount, amount);
        result.min -= padding;
        result.max += padding;
        return result;
    }

    std::unique_ptr<Node> root_;
    std::unordered_map<scene::EntityId, Key> keys_;
    std::unordered_set<scene::EntityId> fallback_entities_;
};

}  // namespace mulan::view::detail
