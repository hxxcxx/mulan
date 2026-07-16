/**
 * @file dynamic_bvh.h
 * @brief 面向动态三维场景的通用增量包围体层次结构。
 * @author hxxcxx
 * @date 2026-07-16
 *
 * 叶节点保存调用方标识与紧致 AABB，内部节点保存子树联合 AABB。插入采用表面积启发式
 * （SAH）选择兄弟节点，并通过局部旋转限制树高；更新和删除只修改受影响路径。
 *
 * Id 只需可复制、可相等比较并可由 Hash 哈希，不要求可默认构造或可排序。查询遍历顺序
 * 不属于接口契约；需要稳定顺序时应由上层按自己的业务键排序。本容器本身不提供并发
 * 读写同步，不同线程共享同一实例时由调用方保证生命周期与互斥。
 */
#pragma once

#include "../algo/intersect.h"
#include "../geom/aabb.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mulan::math {

template <typename Id, typename Hash = std::hash<Id>, typename Equal = std::equal_to<Id>>
class DynamicBVH {
public:
    struct QueryStats {
        size_t nodeBoundsTestCount = 0;
        size_t leafBoundsTestCount = 0;
        size_t resultCount = 0;
    };

    enum class UpdateResult {
        Rejected,
        Unchanged,
        Updated,
    };

    DynamicBVH() = default;

    /** 为 leafCapacity 个叶节点预留连续节点池和查找表容量。 */
    void reserve(size_t leafCapacity) {
        if (leafCapacity > (static_cast<size_t>(kInvalidIndex) + 1u) / 2u) {
            throw std::length_error("DynamicBVH leaf capacity exceeds node index range");
        }
        const size_t nodeCapacity = leafCapacity == 0 ? 0 : leafCapacity * 2u - 1u;
        nodes_.reserve(nodeCapacity);
        leaves_.reserve(leafCapacity);
    }

    /**
     * 插入新叶节点。bounds 非法或 id 已存在时返回 false，且不改变树。
     */
    bool insert(const Id& id, const AABB3& bounds) {
        if (!isValidBounds(bounds) || leaves_.contains(id))
            return false;

        const NodeIndex leaf = allocateNode();
        bool mapped = false;
        try {
            Node& node = nodes_[leaf];
            node.bounds = bounds;
            node.height = 0;
            node.id.emplace(id);

            const auto [_, inserted] = leaves_.emplace(id, leaf);
            if (!inserted) {
                freeNode(leaf);
                return false;
            }
            mapped = true;
            insertLeaf(leaf);
        } catch (...) {
            if (mapped)
                leaves_.erase(id);
            freeNode(leaf);
            throw;
        }
        return true;
    }

    /**
     * 更新已存在叶节点的紧致 AABB。非法 bounds 或未知 id 返回 Rejected。
     */
    UpdateResult update(const Id& id, const AABB3& bounds) {
        if (!isValidBounds(bounds))
            return UpdateResult::Rejected;
        const auto known = leaves_.find(id);
        if (known == leaves_.end())
            return UpdateResult::Rejected;

        Node& leaf = nodes_[known->second];
        if (sameBounds(leaf.bounds, bounds))
            return UpdateResult::Unchanged;

        // 非根叶节点移除时会释放其旧父节点，随后重新插入无需扩容。
        // 因而更新不会在树已被修改后因节点池分配失败而留下半完成状态。
        removeLeaf(known->second);
        leaf.bounds = bounds;
        insertLeaf(known->second);
        return UpdateResult::Updated;
    }

    bool remove(const Id& id) {
        const auto known = leaves_.find(id);
        if (known == leaves_.end())
            return false;
        const NodeIndex leaf = known->second;
        removeLeaf(leaf);
        leaves_.erase(known);
        freeNode(leaf);
        return true;
    }

    void clear() {
        nodes_.clear();
        leaves_.clear();
        root_ = kInvalidIndex;
        freeList_ = kInvalidIndex;
    }

    bool contains(const Id& id) const { return leaves_.contains(id); }

    const AABB3* boundsOf(const Id& id) const {
        const auto known = leaves_.find(id);
        return known == leaves_.end() ? nullptr : &nodes_[known->second].bounds;
    }

    const AABB3& bounds() const {
        static const AABB3 empty;
        return root_ == kInvalidIndex ? empty : nodes_[root_].bounds;
    }

    size_t size() const { return leaves_.size(); }
    bool empty() const { return leaves_.empty(); }
    int height() const { return root_ == kInvalidIndex ? 0 : nodes_[root_].height; }

    /**
     * 射线宽阶段查询。visitor(const Id&) 返回 false 可提前停止遍历；函数返回是否完整遍历。
     * padding 是施加于每个节点 AABB 三轴的非负查询膨胀量，非有限值按 0 处理。
     */
    template <typename Visitor>
    bool queryRay(const Ray3& ray, double padding, Visitor&& visitor, QueryStats* stats = nullptr) const {
        QueryStats local;
        if (root_ == kInvalidIndex || !isFiniteRay(ray)) {
            if (stats)
                *stats = local;
            return true;
        }

        const double safePadding = std::isfinite(padding) && padding > 0.0 ? padding : 0.0;
        std::vector<NodeIndex> stack;
        stack.reserve(static_cast<size_t>(height()) + 1u);
        stack.push_back(root_);
        while (!stack.empty()) {
            const NodeIndex index = stack.back();
            stack.pop_back();
            const Node& node = nodes_[index];
            ++local.nodeBoundsTestCount;
            if (!intersect(ray, expanded(node.bounds, safePadding)).hit)
                continue;

            if (node.isLeaf()) {
                ++local.leafBoundsTestCount;
                ++local.resultCount;
                if (!static_cast<bool>(std::invoke(visitor, *node.id))) {
                    if (stats)
                        *stats = local;
                    return false;
                }
                continue;
            }
            stack.push_back(node.right);
            stack.push_back(node.left);
        }

        if (stats)
            *stats = local;
        return true;
    }

    /** AABB 重叠查询；visitor 的提前终止语义与 queryRay 相同。 */
    template <typename Visitor>
    bool queryBounds(const AABB3& query, Visitor&& visitor, QueryStats* stats = nullptr) const {
        QueryStats local;
        if (root_ == kInvalidIndex || !isValidBounds(query)) {
            if (stats)
                *stats = local;
            return true;
        }

        std::vector<NodeIndex> stack;
        stack.reserve(static_cast<size_t>(height()) + 1u);
        stack.push_back(root_);
        while (!stack.empty()) {
            const NodeIndex index = stack.back();
            stack.pop_back();
            const Node& node = nodes_[index];
            ++local.nodeBoundsTestCount;
            if (!node.bounds.intersects(query))
                continue;

            if (node.isLeaf()) {
                ++local.leafBoundsTestCount;
                ++local.resultCount;
                if (!static_cast<bool>(std::invoke(visitor, *node.id))) {
                    if (stats)
                        *stats = local;
                    return false;
                }
                continue;
            }
            stack.push_back(node.right);
            stack.push_back(node.left);
        }

        if (stats)
            *stats = local;
        return true;
    }

    /**
     * 完整检查父子链接、节点高度、联合 AABB、叶查找表、可达节点和空闲链表。
     * 该接口主要用于测试、断言和离线诊断，不应放在每帧热路径。
     */
    bool validate() const {
        if (root_ == kInvalidIndex) {
            if (!leaves_.empty())
                return false;
        } else if (root_ >= nodes_.size() || nodes_[root_].parent != kInvalidIndex) {
            return false;
        }

        std::unordered_set<NodeIndex> reachable;
        ValidationResult result;
        if (root_ != kInvalidIndex) {
            result = validateNode(root_, kInvalidIndex, reachable);
            if (!result.valid || result.leafCount != leaves_.size())
                return false;
            if (result.nodeCount != result.leafCount * 2u - 1u)
                return false;
        }

        std::unordered_set<NodeIndex> freeNodes;
        NodeIndex free = freeList_;
        while (free != kInvalidIndex) {
            if (free >= nodes_.size() || reachable.contains(free) || !freeNodes.insert(free).second)
                return false;
            if (nodes_[free].height != -1 || nodes_[free].id.has_value())
                return false;
            free = nodes_[free].next;
        }
        return reachable.size() + freeNodes.size() == nodes_.size();
    }

    static bool isValidBounds(const AABB3& bounds) {
        return std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) && std::isfinite(bounds.min.z) &&
               std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) && std::isfinite(bounds.max.z) &&
               bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
    }

private:
    using NodeIndex = uint32_t;
    static constexpr NodeIndex kInvalidIndex = std::numeric_limits<NodeIndex>::max();

    struct Node {
        AABB3 bounds;
        std::optional<Id> id;
        NodeIndex parent = kInvalidIndex;
        NodeIndex left = kInvalidIndex;
        NodeIndex right = kInvalidIndex;
        NodeIndex next = kInvalidIndex;
        int height = -1;

        bool isLeaf() const { return left == kInvalidIndex; }
    };

    struct ValidationResult {
        bool valid = true;
        AABB3 bounds;
        int height = 0;
        size_t leafCount = 0;
        size_t nodeCount = 0;
    };

    static bool sameBounds(const AABB3& lhs, const AABB3& rhs) {
        return lhs.min.x == rhs.min.x && lhs.min.y == rhs.min.y && lhs.min.z == rhs.min.z && lhs.max.x == rhs.max.x &&
               lhs.max.y == rhs.max.y && lhs.max.z == rhs.max.z;
    }

    static AABB3 combined(const AABB3& lhs, const AABB3& rhs) {
        AABB3 result = lhs;
        result.expand(rhs);
        return result;
    }

    static double surfaceArea(const AABB3& bounds) {
        const Vec3 size = bounds.size();
        return 2.0 * (size.x * size.y + size.x * size.z + size.y * size.z);
    }

    static AABB3 expanded(const AABB3& bounds, double amount) {
        if (amount <= 0.0)
            return bounds;
        AABB3 result = bounds;
        const Vec3 padding(amount, amount, amount);
        result.min -= padding;
        result.max += padding;
        return result;
    }

    static bool isFiniteRay(const Ray3& ray) {
        return std::isfinite(ray.origin.x) && std::isfinite(ray.origin.y) && std::isfinite(ray.origin.z) &&
               std::isfinite(ray.direction.x) && std::isfinite(ray.direction.y) && std::isfinite(ray.direction.z);
    }

    NodeIndex allocateNode() {
        if (freeList_ != kInvalidIndex) {
            const NodeIndex index = freeList_;
            freeList_ = nodes_[index].next;
            nodes_[index] = Node{};
            return index;
        }
        if (nodes_.size() >= kInvalidIndex)
            throw std::length_error("DynamicBVH node index range exhausted");
        nodes_.emplace_back();
        return static_cast<NodeIndex>(nodes_.size() - 1u);
    }

    void freeNode(NodeIndex index) {
        Node& node = nodes_[index];
        node = Node{};
        node.next = freeList_;
        freeList_ = index;
    }

    NodeIndex chooseBestSibling(const AABB3& leafBounds) const {
        NodeIndex index = root_;
        while (!nodes_[index].isLeaf()) {
            const Node& node = nodes_[index];
            const double area = surfaceArea(node.bounds);
            const AABB3 combinedBounds = combined(node.bounds, leafBounds);
            const double combinedArea = surfaceArea(combinedBounds);
            const double directCost = 2.0 * combinedArea;
            const double inheritedCost = 2.0 * (combinedArea - area);

            const Node& left = nodes_[node.left];
            const AABB3 leftCombined = combined(left.bounds, leafBounds);
            const double leftCost =
                    (left.isLeaf() ? surfaceArea(leftCombined) : surfaceArea(leftCombined) - surfaceArea(left.bounds)) +
                    inheritedCost;

            const Node& right = nodes_[node.right];
            const AABB3 rightCombined = combined(right.bounds, leafBounds);
            const double rightCost = (right.isLeaf() ? surfaceArea(rightCombined)
                                                     : surfaceArea(rightCombined) - surfaceArea(right.bounds)) +
                                     inheritedCost;

            if (directCost < leftCost && directCost < rightCost)
                break;
            index = leftCost <= rightCost ? node.left : node.right;
        }
        return index;
    }

    void insertLeaf(NodeIndex leaf) {
        if (root_ == kInvalidIndex) {
            root_ = leaf;
            nodes_[leaf].parent = kInvalidIndex;
            return;
        }

        const NodeIndex sibling = chooseBestSibling(nodes_[leaf].bounds);
        const NodeIndex oldParent = nodes_[sibling].parent;
        const NodeIndex newParent = allocateNode();
        Node& parent = nodes_[newParent];
        parent.parent = oldParent;
        parent.bounds = combined(nodes_[sibling].bounds, nodes_[leaf].bounds);
        parent.height = nodes_[sibling].height + 1;
        parent.left = sibling;
        parent.right = leaf;

        nodes_[sibling].parent = newParent;
        nodes_[leaf].parent = newParent;
        if (oldParent == kInvalidIndex) {
            root_ = newParent;
        } else if (nodes_[oldParent].left == sibling) {
            nodes_[oldParent].left = newParent;
        } else {
            nodes_[oldParent].right = newParent;
        }

        NodeIndex index = newParent;
        while (index != kInvalidIndex) {
            index = balance(index);
            refresh(index);
            index = nodes_[index].parent;
        }
    }

    void removeLeaf(NodeIndex leaf) {
        if (leaf == root_) {
            root_ = kInvalidIndex;
            nodes_[leaf].parent = kInvalidIndex;
            return;
        }

        const NodeIndex parent = nodes_[leaf].parent;
        const NodeIndex grandParent = nodes_[parent].parent;
        const NodeIndex sibling = nodes_[parent].left == leaf ? nodes_[parent].right : nodes_[parent].left;
        if (grandParent == kInvalidIndex) {
            root_ = sibling;
            nodes_[sibling].parent = kInvalidIndex;
            freeNode(parent);
        } else {
            if (nodes_[grandParent].left == parent)
                nodes_[grandParent].left = sibling;
            else
                nodes_[grandParent].right = sibling;
            nodes_[sibling].parent = grandParent;
            freeNode(parent);

            NodeIndex index = grandParent;
            while (index != kInvalidIndex) {
                index = balance(index);
                refresh(index);
                index = nodes_[index].parent;
            }
        }
        nodes_[leaf].parent = kInvalidIndex;
    }

    void refresh(NodeIndex index) {
        Node& node = nodes_[index];
        if (node.isLeaf()) {
            node.height = 0;
            return;
        }
        const Node& left = nodes_[node.left];
        const Node& right = nodes_[node.right];
        node.height = 1 + std::max(left.height, right.height);
        node.bounds = combined(left.bounds, right.bounds);
    }

    NodeIndex balance(NodeIndex indexA) {
        Node& a = nodes_[indexA];
        if (a.isLeaf() || a.height < 2)
            return indexA;

        const NodeIndex indexB = a.left;
        const NodeIndex indexC = a.right;
        const int delta = nodes_[indexC].height - nodes_[indexB].height;
        if (delta > 1) {
            const NodeIndex indexF = nodes_[indexC].left;
            const NodeIndex indexG = nodes_[indexC].right;
            Node& c = nodes_[indexC];
            c.left = indexA;
            c.parent = a.parent;
            a.parent = indexC;
            replaceParentChild(c.parent, indexA, indexC);

            if (nodes_[indexF].height > nodes_[indexG].height) {
                c.right = indexF;
                a.right = indexG;
                nodes_[indexF].parent = indexC;
                nodes_[indexG].parent = indexA;
            } else {
                c.right = indexG;
                a.right = indexF;
                nodes_[indexG].parent = indexC;
                nodes_[indexF].parent = indexA;
            }
            refresh(indexA);
            refresh(indexC);
            return indexC;
        }

        if (delta < -1) {
            const NodeIndex indexD = nodes_[indexB].left;
            const NodeIndex indexE = nodes_[indexB].right;
            Node& b = nodes_[indexB];
            b.left = indexA;
            b.parent = a.parent;
            a.parent = indexB;
            replaceParentChild(b.parent, indexA, indexB);

            if (nodes_[indexD].height > nodes_[indexE].height) {
                b.right = indexD;
                a.left = indexE;
                nodes_[indexD].parent = indexB;
                nodes_[indexE].parent = indexA;
            } else {
                b.right = indexE;
                a.left = indexD;
                nodes_[indexE].parent = indexB;
                nodes_[indexD].parent = indexA;
            }
            refresh(indexA);
            refresh(indexB);
            return indexB;
        }
        return indexA;
    }

    void replaceParentChild(NodeIndex parent, NodeIndex oldChild, NodeIndex newChild) {
        if (parent == kInvalidIndex) {
            root_ = newChild;
        } else if (nodes_[parent].left == oldChild) {
            nodes_[parent].left = newChild;
        } else {
            nodes_[parent].right = newChild;
        }
    }

    ValidationResult validateNode(NodeIndex index, NodeIndex expectedParent,
                                  std::unordered_set<NodeIndex>& reachable) const {
        ValidationResult invalid;
        invalid.valid = false;
        if (index >= nodes_.size() || !reachable.insert(index).second)
            return invalid;

        const Node& node = nodes_[index];
        if (node.height < 0 || node.parent != expectedParent)
            return invalid;
        if (node.isLeaf()) {
            if (node.right != kInvalidIndex || node.height != 0 || !node.id.has_value() || !isValidBounds(node.bounds))
                return invalid;
            const auto known = leaves_.find(*node.id);
            if (known == leaves_.end() || known->second != index)
                return invalid;
            return { true, node.bounds, 0, 1, 1 };
        }
        if (node.right == kInvalidIndex || node.id.has_value())
            return invalid;

        ValidationResult left = validateNode(node.left, index, reachable);
        ValidationResult right = validateNode(node.right, index, reachable);
        if (!left.valid || !right.valid)
            return invalid;
        const AABB3 expectedBounds = combined(left.bounds, right.bounds);
        const int expectedHeight = 1 + std::max(left.height, right.height);
        if (node.height != expectedHeight || !sameBounds(node.bounds, expectedBounds))
            return invalid;
        return { true, expectedBounds, expectedHeight, left.leafCount + right.leafCount,
                 left.nodeCount + right.nodeCount + 1u };
    }

    std::vector<Node> nodes_;
    std::unordered_map<Id, NodeIndex, Hash, Equal> leaves_;
    NodeIndex root_ = kInvalidIndex;
    NodeIndex freeList_ = kInvalidIndex;
};

}  // namespace mulan::math
