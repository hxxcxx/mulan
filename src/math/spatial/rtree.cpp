#include "rtree.h"
#include "../math.h"

namespace mulan::math {

// ============================================================
// RTree
// ============================================================

bool RTree::remove(int dataId) {
    auto it = idToRect_.find(dataId);
    if (it == idToRect_.end())
        return false;
    idToRect_.erase(it);
    // 同步从 rects_ 删除
    for (auto rit = rects_.begin(); rit != rects_.end(); ++rit) {
        if (rit->second == dataId) {
            rects_.erase(rit);
            break;
        }
    }
    // 整树重建
    root_ = std::make_unique<RTreeNode>();
    size_ = 0;
    std::vector<std::pair<AABB2, int>> snapshot = rects_;
    rects_.clear();
    idToRect_.clear();
    for (const auto& r : snapshot)
        insert(r.first, r.second);
    return true;
}

std::vector<int> RTree::rangeQuery(const AABB2& range) const {
    std::vector<int> out;
    rangeQueryHelper(root_.get(), range, out);
    return out;
}

std::vector<int> RTree::pointQuery(const Point2& point) const {
    std::vector<int> out;
    pointQueryHelper(root_.get(), point, out);
    return out;
}

// ---------- 插入 ----------

void RTree::insertEntry(const AABB2& /*bounds*/, int /*dataId*/) {
    // 从全量 rects_ 重建整个树
    rebuildFrom(rects_);
}

void RTree::rebuildFrom(const std::vector<std::pair<AABB2, int>>& data) {
    root_ = std::make_unique<RTreeNode>();
    if (data.empty())
        return;
    // 构建入口：若条目数 ≤ maxEntries，直接作为叶节点
    buildNode(root_.get(), data, 0);
}

void RTree::buildNode(RTreeNode* node, std::vector<std::pair<AABB2, int>> entries, int depth) {
    (void) depth;
    const int n = static_cast<int>(entries.size());
    if (n <= maxEntries_) {
        // 叶节点
        for (auto& e : entries) {
            node->entries.push_back(makeLeafEntry(e.first, e.second));
        }
        node->recalcMbr();
        return;
    }
    // 选最长轴
    AABB2 tot{};
    for (const auto& e : entries)
        tot.expand(e.first);
    double dx = tot.max.x - tot.min.x;
    double dy = tot.max.y - tot.min.y;
    int axis = (dx >= dy) ? 0 : 1;

    // 按 MBR 中心在该轴上的坐标排序
    std::sort(entries.begin(), entries.end(), [axis](const auto& a, const auto& b) {
        double ca = (axis == 0) ? (a.first.min.x + a.first.max.x) * 0.5 : (a.first.min.y + a.first.max.y) * 0.5;
        double cb = (axis == 0) ? (b.first.min.x + b.first.max.x) * 0.5 : (b.first.min.y + b.first.max.y) * 0.5;
        return ca < cb;
    });
    int mid = n / 2;
    std::vector<std::pair<AABB2, int>> leftEntries(entries.begin(), entries.begin() + mid);
    std::vector<std::pair<AABB2, int>> rightEntries(entries.begin() + mid, entries.end());

    node->isLeaf = false;
    auto leftChild = std::make_unique<RTreeNode>();
    buildNode(leftChild.get(), leftEntries, depth + 1);
    node->entries.push_back(makeInternalEntry(std::move(leftChild)));

    auto rightChild = std::make_unique<RTreeNode>();
    buildNode(rightChild.get(), rightEntries, depth + 1);
    node->entries.push_back(makeInternalEntry(std::move(rightChild)));

    node->recalcMbr();
}

RTreeNode::Entry RTree::makeLeafEntry(const AABB2& mbr, int dataId) {
    RTreeNode::Entry e;
    e.mbr = mbr;
    e.dataId = dataId;
    return e;
}

RTreeNode::Entry RTree::makeInternalEntry(std::unique_ptr<RTreeNode> child) {
    RTreeNode::Entry e;
    child->recalcMbr();
    e.mbr = child->mbr;
    e.child = std::move(child);
    return e;
}

// ---------- 查询 ----------

void RTree::rangeQueryHelper(const RTreeNode* node, const AABB2& range, std::vector<int>& out) const {
    if (!node)
        return;
    for (const auto& e : node->entries) {
        if (!e.mbr.intersects(range))
            continue;
        if (e.isLeaf()) {
            out.push_back(e.dataId);
        } else {
            rangeQueryHelper(e.child.get(), range, out);
        }
    }
}

void RTree::pointQueryHelper(const RTreeNode* node, const Point2& point, std::vector<int>& out) const {
    if (!node)
        return;
    for (const auto& e : node->entries) {
        if (!e.mbr.contains(point))
            continue;
        if (e.isLeaf()) {
            out.push_back(e.dataId);
        } else {
            pointQueryHelper(e.child.get(), point, out);
        }
    }
}

}  // namespace mulan::math
