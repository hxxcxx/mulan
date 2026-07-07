#include "kdtree.h"
#include "../math.h"

namespace mulan::math {

// ============================================================
// KDTreeNode
// ============================================================

bool KDTreeNode::insert(const Point2& p, int depth) {
    int axis = depth % 2;
    double cur = (axis == 0) ? point.x : point.y;
    double pv = (axis == 0) ? p.x : p.y;
    const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
    if (point.distanceSq(p) <= eps2)
        return false;  // 重复点
    if (pv < cur) {
        if (left)
            return left->insert(p, depth + 1);
        left = std::make_unique<KDTreeNode>(p);
        return true;
    }
    if (right)
        return right->insert(p, depth + 1);
    right = std::make_unique<KDTreeNode>(p);
    return true;
}

bool KDTreeNode::contains(const Point2& p, int depth) const {
    const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
    if (point.distanceSq(p) <= eps2)
        return true;
    int axis = depth % 2;
    double cur = (axis == 0) ? point.x : point.y;
    double pv = (axis == 0) ? p.x : p.y;
    if (pv < cur)
        return left && left->contains(p, depth + 1);
    return right && right->contains(p, depth + 1);
}

void KDTreeNode::rangeQuery(const AABB2& box, int depth, std::vector<Point2>& out) const {
    if (box.contains(point))
        out.push_back(point);
    int axis = depth % 2;
    double cur = (axis == 0) ? point.x : point.y;
    double lo = (axis == 0) ? box.min.x : box.min.y;
    double hi = (axis == 0) ? box.max.x : box.max.y;
    if (lo <= cur && left)
        left->rangeQuery(box, depth + 1, out);
    if (hi >= cur && right)
        right->rangeQuery(box, depth + 1, out);
}

void KDTreeNode::radiusQuery(const Point2& center, double radiusSq, int depth, std::vector<Point2>& out) const {
    if (point.distanceSq(center) <= radiusSq)
        out.push_back(point);
    int axis = depth % 2;
    double cur = (axis == 0) ? point.x : point.y;
    double cv = (axis == 0) ? center.x : center.y;
    double splitDistSq = (cur - cv) * (cur - cv);
    if (cv < cur) {
        if (left)
            left->radiusQuery(center, radiusSq, depth + 1, out);
        if (splitDistSq <= radiusSq && right)
            right->radiusQuery(center, radiusSq, depth + 1, out);
    } else {
        if (right)
            right->radiusQuery(center, radiusSq, depth + 1, out);
        if (splitDistSq <= radiusSq && left)
            left->radiusQuery(center, radiusSq, depth + 1, out);
    }
}

void KDTreeNode::nn(const Point2& query, int depth, double& bestSq, Point2& best) const {
    double d = point.distanceSq(query);
    if (d < bestSq) {
        bestSq = d;
        best = point;
    }
    int axis = depth % 2;
    double cur = (axis == 0) ? point.x : point.y;
    double qv = (axis == 0) ? query.x : query.y;
    double splitDistSq = (cur - qv) * (cur - qv);
    KDTreeNode* nearNode = (qv < cur) ? left.get() : right.get();
    KDTreeNode* farNode = (qv < cur) ? right.get() : left.get();
    if (nearNode)
        nearNode->nn(query, depth + 1, bestSq, best);
    if (splitDistSq < bestSq && farNode)
        farNode->nn(query, depth + 1, bestSq, best);
}

void KDTreeNode::knn(const Point2& query, int k, int depth, std::vector<std::pair<Point2, double>>& heap) const {
    double d = point.distanceSq(query);
    auto cmp = [](const std::pair<Point2, double>& a, const std::pair<Point2, double>& b) {
        return a.second < b.second;
    };  // 最大堆语义
    if (static_cast<int>(heap.size()) < k) {
        heap.push_back({ point, d });
        std::push_heap(heap.begin(), heap.end(), cmp);
    } else if (d < heap.front().second) {
        std::pop_heap(heap.begin(), heap.end(), cmp);
        heap.back() = { point, d };
        std::push_heap(heap.begin(), heap.end(), cmp);
    }
    int axis = depth % 2;
    double cur = (axis == 0) ? point.x : point.y;
    double qv = (axis == 0) ? query.x : query.y;
    double splitDistSq = (cur - qv) * (cur - qv);
    KDTreeNode* nearNode = (qv < cur) ? left.get() : right.get();
    KDTreeNode* farNode = (qv < cur) ? right.get() : left.get();
    if (nearNode)
        nearNode->knn(query, k, depth + 1, heap);
    double worst = (static_cast<int>(heap.size()) < k) ? std::numeric_limits<double>::max() : heap.front().second;
    if (splitDistSq < worst && farNode)
        farNode->knn(query, k, depth + 1, heap);
}

std::unique_ptr<KDTreeNode> KDTreeNode::buildRange(std::vector<Point2>& pts, int start, int end, int depth) {
    if (start >= end)
        return nullptr;
    int axis = depth % 2;
    int mid = start + (end - start) / 2;
    auto byAxis = [axis](const Point2& a, const Point2& b) {
        return (axis == 0) ? (a.x < b.x) : (a.y < b.y);
    };
    std::nth_element(pts.begin() + start, pts.begin() + mid, pts.begin() + end, byAxis);
    auto node = std::make_unique<KDTreeNode>(pts[mid]);
    node->left = buildRange(pts, start, mid, depth + 1);
    node->right = buildRange(pts, mid + 1, end, depth + 1);
    return node;
}

// ============================================================
// KDTree
// ============================================================

void KDTree::build(const std::vector<Point2>& points) {
    root_.reset();
    size_ = 0;
    std::vector<Point2> pts = points;
    // 去重（O(n²)，对构造期可接受；如需更快可用排序去重）
    std::vector<Point2> uniq;
    uniq.reserve(pts.size());
    for (const Point2& p : pts) {
        bool dup = false;
        for (const Point2& q : uniq) {
            if (p.distanceSq(q) <= defaultTolerance().lengthEps * defaultTolerance().lengthEps) {
                dup = true;
                break;
            }
        }
        if (!dup)
            uniq.push_back(p);
    }
    root_ = KDTreeNode::buildRange(uniq, 0, static_cast<int>(uniq.size()), 0);
    size_ = uniq.size();
}

bool KDTree::insert(const Point2& p) {
    if (!root_) {
        root_ = std::make_unique<KDTreeNode>(p);
        ++size_;
        return true;
    }
    if (root_->insert(p, 0)) {
        ++size_;
        return true;
    }
    return false;
}

void KDTree::insert(const std::vector<Point2>& pts) {
    for (const Point2& p : pts)
        insert(p);
}

bool KDTree::nearestNeighbor(const Point2& query, Point2& nearest) const {
    if (!root_)
        return false;
    double bestSq = std::numeric_limits<double>::max();
    Point2 best{};
    root_->nn(query, 0, bestSq, best);
    nearest = best;
    return true;
}

std::vector<Point2> KDTree::kNearestNeighbors(const Point2& query, int k) const {
    std::vector<Point2> result;
    if (k <= 0 || !root_)
        return result;
    std::vector<std::pair<Point2, double>> heap;
    root_->knn(query, k, 0, heap);
    std::sort(heap.begin(), heap.end(), [](const std::pair<Point2, double>& a, const std::pair<Point2, double>& b) {
        return a.second < b.second;
    });
    result.reserve(heap.size());
    for (auto& c : heap)
        result.push_back(c.first);
    return result;
}

}  // namespace mulan::math
