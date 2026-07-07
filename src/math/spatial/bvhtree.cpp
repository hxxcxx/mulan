#include "bvhtree.h"
#include "../math.h"

namespace mulan::math {

// ============================================================
// BVHNode
// ============================================================

std::unique_ptr<BVHNode> BVHNode::build(std::vector<Point2>& pts, int start, int end, int capacity) {
    if (start >= end)
        return nullptr;
    // 计算包围盒
    AABB2 box = AABB2::empty();
    for (int i = start; i < end; ++i)
        box.expand(pts[i]);
    int count = end - start;
    if (count <= capacity) {
        std::vector<Point2> nodePts(pts.begin() + start, pts.begin() + end);
        return std::make_unique<BVHNode>(box, std::move(nodePts));
    }
    // 选最大轴，按中位数分割
    Vec2 sz = box.size();
    int axis = (sz.x >= sz.y) ? 0 : 1;
    int mid = start + count / 2;
    auto byAxis = [axis](const Point2& a, const Point2& b) {
        return (axis == 0) ? (a.x < b.x) : (a.y < b.y);
    };
    std::nth_element(pts.begin() + start, pts.begin() + mid, pts.begin() + end, byAxis);
    auto node = std::make_unique<BVHNode>(box);
    node->left = build(pts, start, mid, capacity);
    node->right = build(pts, mid, end, capacity);
    return node;
}

bool BVHNode::contains(const Point2& p) const {
    if (!bounds.contains(p))
        return false;
    if (isLeaf()) {
        const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
        for (const Point2& q : points)
            if (q.distanceSq(p) <= eps2)
                return true;
        return false;
    }
    return left->contains(p) || right->contains(p);
}

void BVHNode::rangeQuery(const AABB2& box, std::vector<Point2>& out) const {
    if (!bounds.intersects(box))
        return;
    if (isLeaf()) {
        for (const Point2& q : points)
            if (box.contains(q))
                out.push_back(q);
        return;
    }
    left->rangeQuery(box, out);
    right->rangeQuery(box, out);
}

void BVHNode::radiusQuery(const Point2& c, double rsq, std::vector<Point2>& out) const {
    if (bvh_detail::aabb2DistSq(c, bounds) > rsq)
        return;
    if (isLeaf()) {
        for (const Point2& q : points)
            if (q.distanceSq(c) <= rsq)
                out.push_back(q);
        return;
    }
    left->radiusQuery(c, rsq, out);
    right->radiusQuery(c, rsq, out);
}

void BVHNode::nn(const Point2& query, double& bestSq, Point2& best, bool& found) const {
    if (found && bvh_detail::aabb2DistSq(query, bounds) > bestSq)
        return;
    if (isLeaf()) {
        for (const Point2& q : points) {
            double d = q.distanceSq(query);
            if (d < bestSq) {
                bestSq = d;
                best = q;
                found = true;
            }
        }
        return;
    }
    // 近侧优先
    BVHNode* nearChild = left.get();
    BVHNode* farChild = right.get();
    if (bvh_detail::aabb2DistSq(query, right->bounds) < bvh_detail::aabb2DistSq(query, left->bounds)) {
        std::swap(nearChild, farChild);
    }
    nearChild->nn(query, bestSq, best, found);
    if (farChild && bvh_detail::aabb2DistSq(query, farChild->bounds) < bestSq) {
        farChild->nn(query, bestSq, best, found);
    }
}

void BVHNode::knn(const Point2& query, int k, std::vector<std::pair<Point2, double>>& heap) const {
    auto cmp = [](const auto& a, const auto& b) {
        return a.second < b.second;
    };
    auto worstSq = [&]() -> double {
        return static_cast<int>(heap.size()) < k ? std::numeric_limits<double>::max() : heap.front().second;
    };
    if (bvh_detail::aabb2DistSq(query, bounds) > worstSq())
        return;
    if (isLeaf()) {
        for (const Point2& q : points) {
            double d = q.distanceSq(query);
            if (static_cast<int>(heap.size()) < k) {
                heap.push_back({ q, d });
                std::push_heap(heap.begin(), heap.end(), cmp);
            } else if (d < heap.front().second) {
                std::pop_heap(heap.begin(), heap.end(), cmp);
                heap.back() = { q, d };
                std::push_heap(heap.begin(), heap.end(), cmp);
            }
        }
        return;
    }
    // 近侧优先
    BVHNode* nearChild = left.get();
    BVHNode* farChild = right.get();
    if (bvh_detail::aabb2DistSq(query, right->bounds) < bvh_detail::aabb2DistSq(query, left->bounds)) {
        std::swap(nearChild, farChild);
    }
    nearChild->knn(query, k, heap);
    if (bvh_detail::aabb2DistSq(query, farChild->bounds) < worstSq()) {
        farChild->knn(query, k, heap);
    }
}

// ============================================================
// BVHTree
// ============================================================

void BVHTree::build(const std::vector<Point2>& pts) {
    root_.reset();
    size_ = 0;
    std::vector<Point2> copy = pts;
    // 去重
    std::vector<Point2> uniq;
    uniq.reserve(copy.size());
    const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
    for (const Point2& p : copy) {
        bool dup = false;
        for (const Point2& q : uniq)
            if (p.distanceSq(q) <= eps2) {
                dup = true;
                break;
            }
        if (!dup)
            uniq.push_back(p);
    }
    root_ = BVHNode::build(uniq, 0, static_cast<int>(uniq.size()), capacity_);
    size_ = uniq.size();
    if (!uniq.empty())
        bounds_ = root_->bounds;
}

bool BVHTree::insert(const Point2& p) {
    // 增量插入 = 重建
    std::vector<Point2> all = allPoints();
    const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
    for (const Point2& q : all)
        if (q.distanceSq(p) <= eps2)
            return false;  // 重复
    all.push_back(p);
    build(all);
    return true;
}

bool BVHTree::remove(const Point2& p) {
    std::vector<Point2> all = allPoints();
    auto it = std::remove_if(all.begin(), all.end(), [&](const Point2& q) {
        return q.distanceSq(p) <= defaultTolerance().lengthEps * defaultTolerance().lengthEps;
    });
    if (it == all.end())
        return false;
    all.erase(it, all.end());
    build(all);
    return true;
}

bool BVHTree::nearestNeighbor(const Point2& query, Point2& nearest) const {
    if (!root_)
        return false;
    double bestSq = std::numeric_limits<double>::max();
    Point2 best{};
    bool found = false;
    root_->nn(query, bestSq, best, found);
    if (found)
        nearest = best;
    return found;
}

std::vector<Point2> BVHTree::kNearestNeighbors(const Point2& query, int k) const {
    if (k <= 0 || !root_)
        return {};
    std::vector<std::pair<Point2, double>> heap;
    root_->knn(query, k, heap);
    std::sort(heap.begin(), heap.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    std::vector<Point2> out;
    out.reserve(heap.size());
    for (auto& c : heap)
        out.push_back(c.first);
    return out;
}

}  // namespace mulan::math
