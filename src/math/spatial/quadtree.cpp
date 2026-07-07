#include "quadtree.h"
#include "../math.h"

namespace mulan::math {

// ============================================================
// QuadNode
// ============================================================

bool QuadNode::insert(const Point2& p) {
    if (!bounds.contains(p))
        return false;
    if (isLeaf()) {
        if (static_cast<int>(points.size()) < capacity) {
            points.push_back(p);
            return true;
        }
        subdivide();
    }
    if (nw->insert(p))
        return true;
    if (ne->insert(p))
        return true;
    if (sw->insert(p))
        return true;
    if (se->insert(p))
        return true;
    return false;
}

bool QuadNode::contains(const Point2& p) const {
    if (!bounds.contains(p))
        return false;
    if (isLeaf()) {
        const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
        for (const Point2& q : points)
            if (q.distanceSq(p) <= eps2)
                return true;
        return false;
    }
    return nw->contains(p) || ne->contains(p) || sw->contains(p) || se->contains(p);
}

bool QuadNode::remove(const Point2& p) {
    if (!bounds.contains(p))
        return false;
    if (isLeaf()) {
        const double eps2 = defaultTolerance().lengthEps * defaultTolerance().lengthEps;
        for (auto it = points.begin(); it != points.end(); ++it) {
            if (it->distanceSq(p) <= eps2) {
                points.erase(it);
                return true;
            }
        }
        return false;
    }
    return nw->remove(p) || ne->remove(p) || sw->remove(p) || se->remove(p);
}

void QuadNode::rangeQuery(const AABB2& box, std::vector<Point2>& out) const {
    if (!bounds.intersects(box))
        return;
    if (isLeaf()) {
        for (const Point2& q : points)
            if (box.contains(q))
                out.push_back(q);
        return;
    }
    nw->rangeQuery(box, out);
    ne->rangeQuery(box, out);
    sw->rangeQuery(box, out);
    se->rangeQuery(box, out);
}

void QuadNode::radiusQuery(const Point2& center, double radiusSq, std::vector<Point2>& out) const {
    if (detail::aabb2DistSq(center, bounds) > radiusSq)
        return;
    if (isLeaf()) {
        for (const Point2& q : points)
            if (q.distanceSq(center) <= radiusSq)
                out.push_back(q);
        return;
    }
    nw->radiusQuery(center, radiusSq, out);
    ne->radiusQuery(center, radiusSq, out);
    sw->radiusQuery(center, radiusSq, out);
    se->radiusQuery(center, radiusSq, out);
}

void QuadNode::nn(const Point2& query, double& bestSq, Point2& best, bool& found) const {
    if (found && detail::aabb2DistSq(query, bounds) > bestSq)
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
    QuadNode* kids[4] = { nw.get(), ne.get(), sw.get(), se.get() };
    std::sort(kids, kids + 4, [&](QuadNode* a, QuadNode* b) {
        return detail::aabb2DistSq(query, a->bounds) < detail::aabb2DistSq(query, b->bounds);
    });
    for (QuadNode* c : kids)
        c->nn(query, bestSq, best, found);
}

void QuadNode::knn(const Point2& query, int k, std::vector<std::pair<Point2, double>>& heap) const {
    auto cmp = [](const std::pair<Point2, double>& a, const std::pair<Point2, double>& b) {
        return a.second < b.second;
    };
    auto worstSq = [&]() -> double {
        return static_cast<int>(heap.size()) < k ? std::numeric_limits<double>::max() : heap.front().second;
    };
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
    QuadNode* kids[4] = { nw.get(), ne.get(), sw.get(), se.get() };
    std::sort(kids, kids + 4, [&](QuadNode* a, QuadNode* b) {
        return detail::aabb2DistSq(query, a->bounds) < detail::aabb2DistSq(query, b->bounds);
    });
    for (QuadNode* c : kids) {
        if (detail::aabb2DistSq(query, c->bounds) > worstSq())
            continue;
        c->knn(query, k, heap);
    }
}

void QuadNode::subdivide() {
    Point2 c = bounds.center();
    nw = std::make_unique<QuadNode>(AABB2(Point2(bounds.min.x, c.y), Point2(c.x, bounds.max.y)), capacity);
    ne = std::make_unique<QuadNode>(AABB2(Point2(c.x, c.y), Point2(bounds.max.x, bounds.max.y)), capacity);
    sw = std::make_unique<QuadNode>(AABB2(Point2(bounds.min.x, bounds.min.y), Point2(c.x, c.y)), capacity);
    se = std::make_unique<QuadNode>(AABB2(Point2(c.x, bounds.min.y), Point2(bounds.max.x, c.y)), capacity);
    divided = true;
    std::vector<Point2> old = std::move(points);
    points.clear();
    for (const Point2& p : old) {
        if (nw->bounds.contains(p)) {
            nw->insert(p);
            continue;
        }
        if (ne->bounds.contains(p)) {
            ne->insert(p);
            continue;
        }
        if (sw->bounds.contains(p)) {
            sw->insert(p);
            continue;
        }
        se->insert(p);
    }
}

// ============================================================
// Quadtree
// ============================================================

bool Quadtree::insert(const Point2& p) {
    if (!bounds_.contains(p))
        return false;
    if (root_->insert(p)) {
        ++size_;
        return true;
    }
    return false;
}

bool Quadtree::remove(const Point2& p) {
    if (root_->remove(p)) {
        --size_;
        return true;
    }
    return false;
}

bool Quadtree::nearestNeighbor(const Point2& query, Point2& nearest) const {
    double bestSq = std::numeric_limits<double>::max();
    Point2 best{};
    bool found = false;
    root_->nn(query, bestSq, best, found);
    if (found)
        nearest = best;
    return found;
}

std::vector<Point2> Quadtree::kNearestNeighbors(const Point2& query, int k) const {
    if (k <= 0)
        return {};
    std::vector<std::pair<Point2, double>> heap;
    root_->knn(query, k, heap);
    std::sort(heap.begin(), heap.end(), [](const std::pair<Point2, double>& a, const std::pair<Point2, double>& b) {
        return a.second < b.second;
    });
    std::vector<Point2> out;
    out.reserve(heap.size());
    for (auto& c : heap)
        out.push_back(c.first);
    return out;
}

}  // namespace mulan::math
