#include "bsptree.h"
#include "../math.h"

namespace mulan::math {

// ============================================================
// BSPNode
// ============================================================

void BSPNode::insert(const Point2& p, int capacity, const Tolerance& tol) {
    (void) tol;
    if (isLeaf()) {
        points.push_back(p);
        if (static_cast<int>(points.size()) > capacity)
            subdivide();
        return;
    }
    // 非叶：分发到子节点。COPLANAR 默认走 front
    BSPPlane::Side s = splitPlane.classify(p);
    if (s == BSPPlane::Front || s == BSPPlane::Coplanar)
        frontChild->insert(p, capacity, tol);
    else
        backChild->insert(p, capacity, tol);
}

bool BSPNode::contains(const Point2& p) const {
    const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
    if (isLeaf()) {
        for (const Point2& q : points) {
            if (q.distanceSq(p) <= eps2)
                return true;
        }
        return false;
    }
    BSPPlane::Side s = splitPlane.classify(p);
    if (s == BSPPlane::Front || s == BSPPlane::Coplanar)
        return frontChild->contains(p);
    return backChild->contains(p);
}

void BSPNode::rangeQuery(const AABB2& box, std::vector<Point2>& out) const {
    if (!bounds.intersects(box))
        return;
    if (isLeaf()) {
        for (const Point2& q : points) {
            if (box.contains(q))
                out.push_back(q);
        }
        return;
    }
    frontChild->rangeQuery(box, out);
    backChild->rangeQuery(box, out);
}

void BSPNode::radiusQuery(const Point2& center, double radiusSq, std::vector<Point2>& out) const {
    if (bsp_detail::aabb2DistSq(center, bounds) > radiusSq)
        return;
    if (isLeaf()) {
        for (const Point2& q : points) {
            if (q.distanceSq(center) <= radiusSq)
                out.push_back(q);
        }
        return;
    }
    frontChild->radiusQuery(center, radiusSq, out);
    backChild->radiusQuery(center, radiusSq, out);
}

void BSPNode::nn(const Point2& query, double& bestSq, Point2& best, bool& found) const {
    if (found && bsp_detail::aabb2DistSq(query, bounds) > bestSq)
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
    double dToPlane = splitPlane.signedDistance(query);
    BSPNode* nearSide = (dToPlane >= 0) ? frontChild.get() : backChild.get();
    BSPNode* farSide = (dToPlane >= 0) ? backChild.get() : frontChild.get();
    nearSide->nn(query, bestSq, best, found);
    if (dToPlane * dToPlane < bestSq && farSide) {
        farSide->nn(query, bestSq, best, found);
    }
}

void BSPNode::knn(const Point2& query, int k, std::vector<std::pair<Point2, double>>& heap) const {
    auto cmp = [](const auto& a, const auto& b) {
        return a.second < b.second;
    };
    auto worstSq = [&]() -> double {
        return static_cast<int>(heap.size()) < k ? std::numeric_limits<double>::max() : heap.front().second;
    };
    if (bsp_detail::aabb2DistSq(query, bounds) > worstSq())
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
    double dToPlane = splitPlane.signedDistance(query);
    BSPNode* nearSide = (dToPlane >= 0) ? frontChild.get() : backChild.get();
    BSPNode* farSide = (dToPlane >= 0) ? backChild.get() : frontChild.get();
    nearSide->knn(query, k, heap);
    if (dToPlane * dToPlane < worstSq() && farSide)
        farSide->knn(query, k, heap);
}

void BSPNode::subdivide() {
    // 选跨度最大的轴，中位数分割
    Vec2 sz = bounds.size();
    int axis = (sz.x >= sz.y) ? 0 : 1;
    // 按选中轴排序
    std::sort(points.begin(), points.end(),
              [axis](const Point2& a, const Point2& b) { return (axis == 0) ? (a.x < b.x) : (a.y < b.y); });
    int mid = static_cast<int>(points.size()) / 2;
    double splitVal = (axis == 0) ? points[mid].x : points[mid].y;
    Vec2 normal = (axis == 0) ? Vec2(1.0, 0.0) : Vec2(0.0, 1.0);
    Point2 splitPoint = (axis == 0) ? Point2(splitVal, bounds.center().y) : Point2(bounds.center().x, splitVal);
    splitPlane = BSPPlane(splitPoint, normal);

    // 分割包围盒
    AABB2 frontBounds = bounds, backBounds = bounds;
    if (axis == 0) {
        frontBounds.min.x = splitVal;
        backBounds.max.x = splitVal;
    } else {
        frontBounds.min.y = splitVal;
        backBounds.max.y = splitVal;
    }

    frontChild = std::make_unique<BSPNode>(frontBounds);
    backChild = std::make_unique<BSPNode>(backBounds);

    // 分发点：COPLANAR 走 front
    for (const Point2& p : points) {
        BSPPlane::Side s = splitPlane.classify(p);
        if (s == BSPPlane::Front || s == BSPPlane::Coplanar)
            frontChild->points.push_back(p);
        else
            backChild->points.push_back(p);
    }
    points.clear();
}

// ============================================================
// BSPTree
// ============================================================

bool BSPTree::insert(const Point2& p) {
    if (!bounds_.contains(p))
        return false;
    root_->insert(p, capacity_, defaultTolerance());
    ++size_;
    return true;
}

bool BSPTree::remove(const Point2& p) {
    // 重建：收集除 p 外的所有点，重新构建
    std::vector<Point2> all = allPoints();
    auto it = std::remove_if(all.begin(), all.end(), [&](const Point2& q) {
        return q.distanceSq(p) <= defaultTolerance().lengthEps * defaultTolerance().lengthEps;
    });
    if (it == all.end())
        return false;
    all.erase(it, all.end());
    root_ = std::make_unique<BSPNode>(bounds_);
    size_ = 0;
    for (const Point2& q : all)
        root_->insert(q, capacity_, defaultTolerance());
    size_ = all.size();
    return true;
}

bool BSPTree::nearestNeighbor(const Point2& query, Point2& nearest) const {
    double bestSq = std::numeric_limits<double>::max();
    Point2 best{};
    bool found = false;
    root_->nn(query, bestSq, best, found);
    if (found)
        nearest = best;
    return found;
}

std::vector<Point2> BSPTree::kNearestNeighbors(const Point2& query, int k) const {
    if (k <= 0)
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
