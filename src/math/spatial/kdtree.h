/**
 * @file kdtree.h
 * @brief 2D KD-Tree — 点集的静态/半静态空间索引
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::KDTree（点存储），适配至 mulan::math。
 *
 * 用途：点集的近邻查询（NN / KNN）、范围查询、半径查询。
 *
 * 复杂度（诚实标注）：
 *  - build（中位数构造）：O(n log n)。原实现每层对整段重排序（O(n log²n)），
 *    本实现改用 std::nth_element 按层中位数划分，达到 O(n log n)。
 *  - insert（逐点）：O(log n) 平均，O(n) 最坏（不平衡）。
 *  - rangeQuery / radiusQuery / nearestNeighbor / kNearestNeighbors：
 *    O(log n) 平均（带剪枝），最坏 O(n)。
 *
 * 已知限制（源自原实现，保留并说明）：
 *  - 不支持 remove。删除需求请重建（清空 + build）。
 *  - insert 不做再平衡，大量逐点插入后性能退化；静态点集应优先用 build。
 *  - 重复点（坐标在容差内相等）被拒绝插入。
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

// ============================================================
// KDTreeNode（内部，先于 KDTree 定义以便 KDTree 内联调用其成员）
// ============================================================

class KDTreeNode {
public:
    Point2 point;
    std::unique_ptr<KDTreeNode> left;
    std::unique_ptr<KDTreeNode> right;

    explicit KDTreeNode(const Point2& p) : point(p) {}

    MATH_API bool insert(const Point2& p, int depth);

    MATH_API bool contains(const Point2& p, int depth) const;

    MATH_API void rangeQuery(const AABB2& box, int depth, std::vector<Point2>& out) const;

    MATH_API void radiusQuery(const Point2& center, double radiusSq, int depth, std::vector<Point2>& out) const;

    MATH_API void nn(const Point2& query, int depth, double& bestSq, Point2& best) const;

    MATH_API void knn(const Point2& query, int k, int depth, std::vector<std::pair<Point2, double>>& heap) const;

    void collect(std::vector<Point2>& out) const {
        out.push_back(point);
        if (left)
            left->collect(out);
        if (right)
            right->collect(out);
    }

    int depth() const {
        int ld = left ? left->depth() : 0;
        int rd = right ? right->depth() : 0;
        return 1 + std::max(ld, rd);
    }

    // 用 nth_element 递归构造：[start, end) 中按 axis 取中位数划分子树。O(n log n)。
    MATH_API static std::unique_ptr<KDTreeNode> buildRange(std::vector<Point2>& pts, int start, int end, int depth);
};

// ============================================================
// KDTree
// ============================================================

/// 2D KD-Tree（点集）。
class KDTree {
public:
    KDTree() = default;

    /// 由点集中位数构造（O(n log n)）。内部去重。
    MATH_API void build(const std::vector<Point2>& points);

    /// 插入单点（重复点返回 false）。O(log n) 平均。
    MATH_API bool insert(const Point2& p);

    MATH_API void insert(const std::vector<Point2>& pts);

    /// 是否包含某点（容差内相等）。
    bool contains(const Point2& p) const { return root_ && root_->contains(p, 0); }

    /// 范围查询：返回落在 box 内的所有点。
    std::vector<Point2> rangeQuery(const AABB2& box) const {
        std::vector<Point2> out;
        if (root_)
            root_->rangeQuery(box, 0, out);
        return out;
    }

    /// 半径查询：返回到 center 距离 ≤ radius 的所有点。
    std::vector<Point2> radiusQuery(const Point2& center, double radius) const {
        std::vector<Point2> out;
        if (root_)
            root_->radiusQuery(center, radius * radius, 0, out);
        return out;
    }

    /// 最近邻。返回是否找到（空树返回 false）。
    MATH_API bool nearestNeighbor(const Point2& query, Point2& nearest) const;

    /// k 近邻（按距离升序返回 k 个，不足则返回全部）。
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
    int depth() const { return root_ ? root_->depth() : 0; }

private:
    std::unique_ptr<KDTreeNode> root_;
    size_t size_ = 0;
};

}  // namespace mulan::math
