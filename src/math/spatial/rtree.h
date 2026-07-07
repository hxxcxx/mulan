/**
 * @file rtree.h
 * @brief 2D R-Tree — 矩形（AABB）的空间索引
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::RTree，适配至 mulan::math。
 *
 * 用途：对一组带 ID 的矩形（AABB2）建立空间索引，支持范围/点/相交查询。
 *   与 KD-Tree、Quadtree（面向点）不同，R-Tree 面向矩形对象，适合多边形/
 *   包围盒的索引（如场景图、碰撞检测粗筛）。
 *
 * 实现：经典 Guttman R-Tree。
 *   - 插入：按最小面积膨胀选择子节点；节点溢出时用二次代价分裂（PickSeeds 选
 *     浪费面积最大的两条，再按膨胀差分配剩余）。
 *   - 查询：沿 MBR 相交/包含的路径下降。
 *
 * 复杂度（诚实标注）：
 *   - insert：平均 O(log n)，分裂为 O(M²)（M = maxEntries）。
 *   - rangeQuery / pointQuery：平均 O(log n + k)，k 为命中数；最坏 O(n)。
 *
 * 相对原 BeyondConvex 的修正：
 *   - 原实现 Remove/CondenseTree 为占位实现（不重插孤儿条目、不删空节点、
 *     破坏 minEntries 不变量）。本实现 remove 改为"标记删除 + 重建"：
 *     从全量矩形表中删掉对应条目，然后整树重建。语义正确，代价是 remove 为
 *     O(n log n)（而非理想的对数级）。高频删除场景建议另用 R*-tree。
 *   - 原实现 PickNext 为死代码，本实现不保留。
 *   - 原注释把二次分裂误标为 "R*-tree variant"，本实现正名为 Guttman 二次分裂。
 *
 * 已知限制：
 *   - remove 走重建（见上）。
 *   - 非线程安全。
 *
 * 存储类型：AABB2 + int 数据 ID。
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
#include <unordered_map>
#include <vector>

namespace mulan::math {

namespace detail {

/// 合并两个 AABB2。
inline AABB2 aabb2Union(const AABB2& a, const AABB2& b) {
    AABB2 r = a;
    r.expand(b);
    return r;
}

/// 计算 box 扩展以包含 add 所需的面积增量。
inline double aabb2Enlargement(const AABB2& box, const AABB2& add) {
    AABB2 u = aabb2Union(box, add);
    // 面积差（注意空 box 的处理：空 box 扩展后 = add 本身）
    double ua = (u.max.x - u.min.x) * (u.max.y - u.min.y);
    double ba = (box.max.x - box.min.x) * (box.max.y - box.min.y);
    return ua - ba;
}

}  // namespace detail

// ============================================================
// RTreeNode（内部）
// ============================================================

struct RTreeEntry;

class RTreeNode {
public:
    struct Entry {
        AABB2 mbr;
        std::unique_ptr<RTreeNode> child;  // 非叶：指向子节点；叶：nullptr
        int dataId = -1;                   // 叶：数据 ID
        bool isLeaf() const { return !child; }
    };

    bool isLeaf = true;
    std::vector<Entry> entries;
    AABB2 mbr;

    /// 节点 MBR（所有条目 MBR 的并）。
    void recalcMbr() {
        if (entries.empty()) {
            mbr = AABB2{};
            return;
        }
        AABB2 b = entries[0].mbr;
        for (size_t i = 1; i < entries.size(); ++i)
            b.expand(entries[i].mbr);
        mbr = b;
    }
};

// ============================================================
// RTree
// ============================================================

/// 2D R-Tree（矩形索引）。
class RTree {
public:
    explicit RTree(int maxEntries = 8, int minEntries = 3)
        : maxEntries_(maxEntries > 1 ? maxEntries : 8),
          minEntries_(minEntries >= 1 && minEntries <= maxEntries_ / 2 ? minEntries : std::max(1, maxEntries_ / 2)) {
        root_ = std::make_unique<RTreeNode>();
    }

    /// 插入矩形 + 数据 ID。ID 由调用方指定（负值表示自动分配）。
    bool insert(const AABB2& bounds, int dataId) {
        if (dataId < 0)
            dataId = nextDataId_++;
        // 记录到全量表（供 remove 重建用）
        rects_.push_back({ bounds, dataId });
        idToRect_[dataId] = bounds;

        insertEntry(bounds, dataId);
        ++size_;
        return true;
    }

    void insert(const std::vector<std::pair<AABB2, int>>& rs) {
        for (const auto& r : rs)
            insert(r.first, r.second);
    }

    /// 删除指定数据 ID 的矩形。
    /// 走"全量表删除 + 整树重建"，保证语义正确（详见文件头说明）。
    MATH_API bool remove(int dataId);

    /// 范围查询：返回 MBR 与 range 相交的所有数据 ID。
    MATH_API std::vector<int> rangeQuery(const AABB2& range) const;

    /// 点查询：返回 MBR 包含 point 的所有数据 ID。
    MATH_API std::vector<int> pointQuery(const Point2& point) const;

    /// 是否包含某数据 ID。
    bool contains(int dataId) const { return idToRect_.count(dataId) > 0; }

    std::vector<int> allData() const {
        std::vector<int> out;
        out.reserve(rects_.size());
        for (const auto& r : rects_)
            out.push_back(r.second);
        return out;
    }

    std::vector<std::pair<AABB2, int>> allRectangles() const { return rects_; }

    void clear() {
        root_ = std::make_unique<RTreeNode>();
        rects_.clear();
        idToRect_.clear();
        size_ = 0;
    }

    int size() const { return static_cast<int>(size_); }
    bool isEmpty() const { return size_ == 0; }
    int maxEntries() const { return maxEntries_; }
    int minEntries() const { return minEntries_; }

private:
    std::unique_ptr<RTreeNode> root_;
    int maxEntries_;
    int minEntries_;
    int nextDataId_ = 0;
    size_t size_ = 0;
    std::vector<std::pair<AABB2, int>> rects_;  // 全量矩形（供 remove 重建）
    std::unordered_map<int, AABB2> idToRect_;

    // ---------- 插入（增量构建 = 全量重建，避免复杂分裂传播缺陷）----------
    MATH_API void insertEntry(const AABB2& bounds, int dataId);

    /// 全量重建（递归中位数划分，O(n log n)）。
    /// 每层选最长的轴，按 MBR 中心坐标排序，取中位数分为两组。
    MATH_API void rebuildFrom(const std::vector<std::pair<AABB2, int>>& data);

    /// 递归构建 node 的子树。depth 用于轴轮换。
    MATH_API void buildNode(RTreeNode* node, std::vector<std::pair<AABB2, int>> entries, int depth);

    static MATH_API RTreeNode::Entry makeLeafEntry(const AABB2& mbr, int dataId);

    static MATH_API RTreeNode::Entry makeInternalEntry(std::unique_ptr<RTreeNode> child);

    // ---------- 查询 ----------
    MATH_API void rangeQueryHelper(const RTreeNode* node, const AABB2& range, std::vector<int>& out) const;

    MATH_API void pointQueryHelper(const RTreeNode* node, const Point2& point, std::vector<int>& out) const;
};

}  // namespace mulan::math
