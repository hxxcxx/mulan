/**
 * @file bvhtree.h
 * @brief 2D BVH Tree — 包围盒层次结构（点集索引）
 * @author hxxcxx
 * @date 2026-07-07
 *
 * BVH（Bounding Volume Hierarchy）通过递归包围盒组织几何对象。
 * 本实现存储 Point2，每个节点维护 AABB2 包围盒，叶子存点，非叶子分两子。
 *
 * 分割策略：每层选跨度最大的轴，按中位数分割（与 KD-Tree 类似，但以 AABB2
 * 而非分割平面表达子节点范围）。亦可扩展为 SAH（表面积启发式）分割。
 *
 * 修正自 BeyondConvex::BVHTree：
 *  - KNN 带剪枝（原版无剪枝 → O(n)）
 *  - 单套分割逻辑，无重复 SAH 实现
 *  - 增量 insert 走 rebuild 路径，不退化
 *
 * 复杂度（诚实标注）：
 *  - build / insert（重建）：O(n log n)
 *  - contains / rangeQuery / radiusQuery / NN / KNN：平均 O(log n)
 *  - remove（重建）：O(n log n)
 *
 * 存储类型：Point2。越界点被拒绝（返回 false）。
 */
#pragma once
#include "../math_export.h"

#include "../linalg/vec2.h"
#include "../geom/point.h"
#include "../geom/aabb.h"
#include "../scalar/tolerance.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace mulan::math {

namespace bvh_detail {
/// 点到 AABB2 的最近平方距离。
inline double aabb2DistSq(const Point2& p, const AABB2& b) {
    double dx = 0.0, dy = 0.0;
    if (p.x < b.min.x)
        dx = b.min.x - p.x;
    else if (p.x > b.max.x)
        dx = p.x - b.max.x;
    if (p.y < b.min.y)
        dy = b.min.y - p.y;
    else if (p.y > b.max.y)
        dy = p.y - b.max.y;
    return dx * dx + dy * dy;
}
}  // namespace bvh_detail

// ============================================================
// BVHNode（内部）
// ============================================================

class BVHNode {
public:
    AABB2 bounds;
    std::vector<Point2> points;
    std::unique_ptr<BVHNode> left;
    std::unique_ptr<BVHNode> right;

    explicit BVHNode(const AABB2& bds) : bounds(bds) {}
    BVHNode(const AABB2& bds, std::vector<Point2> pts) : bounds(bds), points(std::move(pts)) {}

    bool isLeaf() const { return !left && !right; }

    /// 递归构建：对 [start, end) 按最大轴中位数分割，直到 ≤ capacity。
    MATH_API static std::unique_ptr<BVHNode> build(std::vector<Point2>& pts, int start, int end, int capacity);

    // ---------- 查询 ----------

    MATH_API bool contains(const Point2& p) const;

    MATH_API void rangeQuery(const AABB2& box, std::vector<Point2>& out) const;

    MATH_API void radiusQuery(const Point2& c, double rsq, std::vector<Point2>& out) const;

    MATH_API void nn(const Point2& query, double& bestSq, Point2& best, bool& found) const;

    MATH_API void knn(const Point2& query, int k, std::vector<std::pair<Point2, double>>& heap) const;

    void collect(std::vector<Point2>& out) const {
        if (isLeaf()) {
            out.insert(out.end(), points.begin(), points.end());
            return;
        }
        left->collect(out);
        right->collect(out);
    }

    int calcDepth() const {
        if (isLeaf())
            return 1;
        return 1 + std::max(left->calcDepth(), right->calcDepth());
    }
};

// ============================================================
// BVHTree
// ============================================================

class BVHTree {
public:
    BVHTree(const AABB2& bounds = AABB2{}, int capacity = 8)
        : bounds_(bounds), capacity_(capacity > 0 ? capacity : 8) {}

    /// 由点集中位数构建（O(n log n)）。
    MATH_API void build(const std::vector<Point2>& pts);

    MATH_API bool insert(const Point2& p);

    MATH_API bool remove(const Point2& p);

    bool contains(const Point2& p) const { return root_ && root_->contains(p); }

    std::vector<Point2> rangeQuery(const AABB2& box) const {
        std::vector<Point2> out;
        if (root_)
            root_->rangeQuery(box, out);
        return out;
    }

    std::vector<Point2> radiusQuery(const Point2& c, double r) const {
        std::vector<Point2> out;
        if (root_)
            root_->radiusQuery(c, r * r, out);
        return out;
    }

    MATH_API bool nearestNeighbor(const Point2& query, Point2& nearest) const;

    MATH_API std::vector<Point2> kNearestNeighbors(const Point2& query, int k) const;

    std::vector<Point2> allPoints() const {
        std::vector<Point2> out;
        if (root_)
            root_->collect(out);
        return out;
    }

    void clear() {
        root_.reset();
        size_ = 0;
    }
    int size() const { return static_cast<int>(size_); }
    bool isEmpty() const { return !root_; }
    int depth() const { return root_ ? root_->calcDepth() : 0; }

private:
    std::unique_ptr<BVHNode> root_;
    AABB2 bounds_;
    int capacity_;
    size_t size_ = 0;
};

}  // namespace mulan::math
