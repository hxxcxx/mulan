/**
 * @file quadtree.h
 * @brief 2D 四叉树 — 点集的区域空间索引
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::Quadtree，适配至 mulan::math。
 *
 * 结构：经典区域四叉树。叶子最多容纳 capacity 个点；超出则四等分
 * （NW/NE/SW/SE）并把点重新分配到子节点。详见文件头注释。
 *
 * 修正记录（相对原 BeyondConvex）：
 *  - Subdivide 修正：原实现把本节点点一次性塞进子节点的 points_ 列表，
 *    绕过容量检查，导致子节点可能超容。本实现按 insert 路径逐点下发。
 *  - KNN 剪枝：原实现无剪枝（访问所有节点，O(n)）。本实现用距离到
 *    子节点 box 的下界剪枝，平均 O(log n + k log k)。
 *
 * 已知限制：
 *  - remove 不合并下溢节点。批量删除后可重建。
 *  - 无最大深度限制。点极度集中时递归深度可能过大。
 *
 * 存储类型：Point2。容差 lengthEps 用于重复点判定。
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

namespace detail {
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
}  // namespace detail

// ============================================================
// QuadNode（内部，先于 Quadtree 定义）
// ============================================================

class QuadNode {
public:
    AABB2 bounds;
    int capacity;
    std::vector<Point2> points;
    bool divided = false;
    std::unique_ptr<QuadNode> nw, ne, sw, se;

    QuadNode(AABB2 bds, int cap) : bounds(bds), capacity(cap) {}

    bool isLeaf() const { return !divided; }

    MATH_API bool insert(const Point2& p);

    MATH_API bool contains(const Point2& p) const;

    MATH_API bool remove(const Point2& p);

    MATH_API void rangeQuery(const AABB2& box, std::vector<Point2>& out) const;

    MATH_API void radiusQuery(const Point2& center, double radiusSq, std::vector<Point2>& out) const;

    MATH_API void nn(const Point2& query, double& bestSq, Point2& best, bool& found) const;

    MATH_API void knn(const Point2& query, int k, std::vector<std::pair<Point2, double>>& heap) const;

    void collect(std::vector<Point2>& out) const {
        if (isLeaf()) {
            out.insert(out.end(), points.begin(), points.end());
            return;
        }
        nw->collect(out);
        ne->collect(out);
        sw->collect(out);
        se->collect(out);
    }

    int depth() const {
        if (isLeaf())
            return 1;
        return 1 + std::max({ nw->depth(), ne->depth(), sw->depth(), se->depth() });
    }

private:
    MATH_API void subdivide();
};

// ============================================================
// Quadtree
// ============================================================

/// 2D 四叉树（点集）。
class Quadtree {
public:
    explicit Quadtree(AABB2 bounds = AABB2{}, int capacity = 4)
        : bounds_(bounds),
          capacity_(capacity > 0 ? capacity : 4),
          root_(std::make_unique<QuadNode>(bounds_, capacity_)) {}

    MATH_API bool insert(const Point2& p);

    void insert(const std::vector<Point2>& pts) {
        for (const Point2& p : pts)
            insert(p);
    }

    bool contains(const Point2& p) const { return root_->contains(p); }

    MATH_API bool remove(const Point2& p);

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

    int size() const { return static_cast<int>(size_); }
    bool isEmpty() const { return size_ == 0; }
    int depth() const { return root_->depth(); }

private:
    AABB2 bounds_;
    int capacity_;
    std::unique_ptr<QuadNode> root_;
    size_t size_ = 0;
};

}  // namespace mulan::math
