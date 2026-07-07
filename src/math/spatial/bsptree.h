/**
 * @file bsptree.h
 * @brief 2D BSP Tree — 平面二分空间分割树（点集索引）
 * @author hxxcxx
 * @date 2026-07-07
 *
 * BSP Tree（Binary Space Partitioning）通过平面递归分割空间。
 * 本实现存储 Point2，分割平面取轴对齐（最大方差轴 → 中位数），
 * 对点集场景等价于 KD-Tree 但接口以平面分类（front/back）表达。
 *
 * 修正自 BeyondConvex::BSPTree：
 *  - COPLANAR 点只分配到 front 侧，不重复插入（原版两子节点均插入 → 重复）
 *  - KNN 带剪枝（原版 KNearestNeighborsHelper 为死代码且无剪枝）
 *  - 增量 insert 走 rebuild 路径，避免退化
 *  - 移除死代码 KNearestNeighborsHelper
 *
 * 复杂度（诚实标注）：
 *  - Insert：重建 O(n log n)
 *  - Remove：重建 O(n log n)
 *  - Contains / RangeQuery / RadiusQuery / NN / KNN：平均 O(log n)
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
#include <limits>
#include <memory>
#include <vector>

namespace mulan::math {

namespace bsp_detail {
/// 点到 AABB2 的最近平方距离（点在盒内为 0）。
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
}  // namespace bsp_detail

/// 2D 平面定义：点 + 法向，用于 BSP 分割分类。
struct BSPPlane {
    Point2 point{};
    Vec2 normal{ 1.0, 0.0 };

    BSPPlane() = default;
    BSPPlane(const Point2& p, const Vec2& n) : point(p), normal(n.normalized()) {}

    double signedDistance(const Point2& p) const { return (p.x - point.x) * normal.x + (p.y - point.y) * normal.y; }

    enum Side { Front, Back, Coplanar };

    Side classify(const Point2& p, double eps = 1e-10) const {
        double d = signedDistance(p);
        if (d > eps)
            return Front;
        if (d < -eps)
            return Back;
        return Coplanar;
    }
};

// ============================================================
// BSPNode（内部）
// ============================================================

class BSPNode {
public:
    AABB2 bounds;
    BSPPlane splitPlane;
    std::vector<Point2> points;
    std::unique_ptr<BSPNode> frontChild;
    std::unique_ptr<BSPNode> backChild;

    BSPNode(const AABB2& bds) : bounds(bds) {}

    bool isLeaf() const { return !frontChild && !backChild; }

    MATH_API void insert(const Point2& p, int capacity, const Tolerance& tol);

    MATH_API bool contains(const Point2& p) const;

    MATH_API void rangeQuery(const AABB2& box, std::vector<Point2>& out) const;

    MATH_API void radiusQuery(const Point2& center, double radiusSq, std::vector<Point2>& out) const;

    MATH_API void nn(const Point2& query, double& bestSq, Point2& best, bool& found) const;

    MATH_API void knn(const Point2& query, int k, std::vector<std::pair<Point2, double>>& heap) const;

    void collect(std::vector<Point2>& out) const {
        if (isLeaf()) {
            out.insert(out.end(), points.begin(), points.end());
            return;
        }
        frontChild->collect(out);
        backChild->collect(out);
    }

    int depth() const {
        if (isLeaf())
            return 1;
        return 1 + std::max(frontChild->depth(), backChild->depth());
    }

    int nodeCount() const {
        if (isLeaf())
            return 1;
        return 1 + frontChild->nodeCount() + backChild->nodeCount();
    }

    int pointCount() const {
        if (isLeaf())
            return static_cast<int>(points.size());
        return frontChild->pointCount() + backChild->pointCount();
    }

private:
    MATH_API void subdivide();
};

// ============================================================
// BSPTree
// ============================================================

/// 2D BSP Tree（点集）。
class BSPTree {
public:
    BSPTree(const AABB2& bounds, int capacity = 8)
        : bounds_(bounds), capacity_(capacity > 0 ? capacity : 8), root_(std::make_unique<BSPNode>(bounds_)) {}

    MATH_API bool insert(const Point2& p);

    void insert(const std::vector<Point2>& pts) {
        for (const Point2& p : pts)
            insert(p);
    }

    MATH_API bool remove(const Point2& p);

    bool contains(const Point2& p) const { return root_->contains(p); }

    std::vector<Point2> rangeQuery(const AABB2& box) const {
        std::vector<Point2> out;
        root_->rangeQuery(box, out);
        return out;
    }

    std::vector<Point2> radiusQuery(const Point2& center, double radius) const {
        std::vector<Point2> out;
        root_->radiusQuery(center, radius * radius, out);
        return out;
    }

    MATH_API bool nearestNeighbor(const Point2& query, Point2& nearest) const;

    MATH_API std::vector<Point2> kNearestNeighbors(const Point2& query, int k) const;

    std::vector<Point2> allPoints() const {
        std::vector<Point2> out;
        root_->collect(out);
        return out;
    }

    void clear() {
        root_ = std::make_unique<BSPNode>(bounds_);
        size_ = 0;
    }
    int size() const { return static_cast<int>(size_); }
    bool isEmpty() const { return size_ == 0; }
    int depth() const { return root_->depth(); }
    int nodeCount() const { return root_->nodeCount(); }

private:
    AABB2 bounds_;
    int capacity_;
    std::unique_ptr<BSPNode> root_;
    size_t size_ = 0;
};

}  // namespace mulan::math
