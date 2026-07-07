/**
 * @file convex_hull.cpp
 * @brief 2D 凸包实现 — ConvexHull 成员 + 三种构造算法
 * @author hxxcxx
 * @date 2026-07-07
 */
#include "convex_hull.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace mulan::math {

// ============================================================
// ConvexHull 成员
// ============================================================

bool ConvexHull::contains(const Point2& p, const Tolerance& tol) const {
    if (vertices_.size() < 3)
        return false;
    const size_t n = vertices_.size();
    for (size_t i = 0; i < n; ++i) {
        const Point2& a = vertices_[i];
        const Point2& b = vertices_[(i + 1) % n];
        if (!toLeftOrOn(a, b, p, tol)) {
            return false;
        }
    }
    return true;
}

std::vector<Segment2> ConvexHull::edges() const {
    std::vector<Segment2> result;
    if (vertices_.size() < 2)
        return result;
    const size_t n = vertices_.size();
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result.emplace_back(vertices_[i], vertices_[(i + 1) % n]);
    }
    return result;
}

double ConvexHull::area() const {
    if (vertices_.size() < 3)
        return 0.0;
    const size_t n = vertices_.size();
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Point2& a = vertices_[i];
        const Point2& b = vertices_[(i + 1) % n];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::abs(sum) * 0.5;
}

double ConvexHull::perimeter() const {
    if (vertices_.size() < 2)
        return 0.0;
    const size_t n = vertices_.size();
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += vertices_[i].distance(vertices_[(i + 1) % n]);
    }
    return sum;
}

// ============================================================
// 内部辅助
// ============================================================
namespace detail {

int findLeftmostLowest(const std::vector<Point2>& points) {
    int idx = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].x < points[idx].x || (points[i].x == points[idx].x && points[i].y < points[idx].y)) {
            idx = static_cast<int>(i);
        }
    }
    return idx;
}

int findLowest(const std::vector<Point2>& points) {
    int idx = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].y < points[idx].y || (points[i].y == points[idx].y && points[i].x < points[idx].x)) {
            idx = static_cast<int>(i);
        }
    }
    return idx;
}

bool isMoreCounterClockwise(const Point2& origin, const Point2& candidate, const Point2& currentBest,
                            const Tolerance& tol) {
    Orientation o = orientation(origin, candidate, currentBest, tol);
    if (o == Orientation::Left) {
        return true;
    }
    if (o == Orientation::Collinear) {
        return candidate.distanceSq(origin) > currentBest.distanceSq(origin);
    }
    return false;
}

void buildHalfHull(std::vector<Point2>& half, const Point2& p, const Tolerance& tol) {
    while (half.size() >= 2) {
        const Point2& top = half.back();
        const Point2& secondTop = half[half.size() - 2];
        Vec2 v1 = top - secondTop;
        Vec2 v2 = p - top;
        double cross = v1.cross(v2);
        if (cross <= tol.lengthEps * tol.lengthEps) {
            half.pop_back();
        } else {
            break;
        }
    }
    half.push_back(p);
}

}  // namespace detail

// ============================================================
// convexHull 分发
// ============================================================

ConvexHull convexHull(const std::vector<Point2>& points, ConvexHullAlgorithm algo, const Tolerance& tol) {
    switch (algo) {
    case ConvexHullAlgorithm::JarvisMarch: return convexHullJarvisMarch(points, tol);
    case ConvexHullAlgorithm::GrahamScan: return convexHullGrahamScan(points, tol);
    case ConvexHullAlgorithm::MonotoneChain: return convexHullMonotoneChain(points, tol);
    }
    throw std::invalid_argument("unknown ConvexHullAlgorithm");
}

// ============================================================
// Jarvis March（礼品包扎）O(nh)
// ============================================================

ConvexHull convexHullJarvisMarch(const std::vector<Point2>& points, const Tolerance& tol) {
    if (points.size() < 3)
        return ConvexHull{};

    std::vector<Point2> hull;
    const int n = static_cast<int>(points.size());
    const int start = detail::findLeftmostLowest(points);
    int current = start;

    do {
        hull.push_back(points[current]);

        int next = (current + 1) % n;
        for (int i = 0; i < n; ++i) {
            if (i == current)
                continue;
            if (detail::isMoreCounterClockwise(points[current], points[i], points[next], tol)) {
                next = i;
            }
        }
        current = next;
    } while (current != start);

    return ConvexHull(std::move(hull));
}

// ============================================================
// Graham Scan O(n log n)
// ============================================================

ConvexHull convexHullGrahamScan(const std::vector<Point2>& points, const Tolerance& tol) {
    if (points.size() < 3)
        return ConvexHull{};

    const int pivotIdx = detail::findLowest(points);
    const Point2 pivot = points[pivotIdx];

    // 复制除 pivot 外的点
    std::vector<Point2> sorted;
    sorted.reserve(points.size() - 1);
    for (size_t i = 0; i < points.size(); ++i) {
        if (static_cast<int>(i) != pivotIdx) {
            sorted.push_back(points[i]);
        }
    }

    // 极角排序
    std::sort(sorted.begin(), sorted.end(),
              [&](const Point2& a, const Point2& b) { return detail::compareByPolarAngle(pivot, a, b); });

    // 共线去重：每组共线点仅保留最远者（保证凸包含边界极点）
    if (!sorted.empty()) {
        size_t lastUnique = 0;
        for (size_t i = 1; i < sorted.size(); ++i) {
            Vec2 v1 = sorted[lastUnique] - pivot;
            Vec2 v2 = sorted[i] - pivot;
            double cross = v1.cross(v2);
            if (std::abs(cross) < 1e-12) {
                // 共线：保留更远者
                if (v2.lengthSq() > v1.lengthSq()) {
                    sorted[lastUnique] = sorted[i];
                }
            } else {
                ++lastUnique;
                if (lastUnique != i) {
                    sorted[lastUnique] = sorted[i];
                }
            }
        }
        sorted.resize(lastUnique + 1);
    }

    // 扫描构建（栈）
    std::vector<Point2> stack;
    stack.push_back(pivot);
    if (!sorted.empty()) {
        stack.push_back(sorted[0]);
    }
    for (size_t i = 1; i < sorted.size(); ++i) {
        while (stack.size() >= 2) {
            const Point2& top = stack[stack.size() - 1];
            const Point2& secondTop = stack[stack.size() - 2];
            // 非严格左转（右转或共线）即弹栈，保证严格凸
            if (!toLeft(secondTop, top, sorted[i], tol)) {
                stack.pop_back();
            } else {
                break;
            }
        }
        stack.push_back(sorted[i]);
    }

    return ConvexHull(std::move(stack));
}

// ============================================================
// Monotone Chain O(n log n)
// ============================================================

ConvexHull convexHullMonotoneChain(const std::vector<Point2>& points, const Tolerance& tol) {
    if (points.size() < 3) {
        return ConvexHull{};
    }

    // 按字典序排序（去重可选，这里保留原行为：不去重）
    std::vector<Point2> sorted = points;
    std::sort(sorted.begin(), sorted.end(), detail::lexicoLess);

    // 下凸包
    std::vector<Point2> lower;
    lower.reserve(sorted.size());
    for (const Point2& p : sorted) {
        detail::buildHalfHull(lower, p, tol);
    }

    // 上凸包（逆序遍历）
    std::vector<Point2> upper;
    upper.reserve(sorted.size());
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        detail::buildHalfHull(upper, *it, tol);
    }

    // 合并：下凸包去末点 + 上凸包去末点（首末各重复一次）
    std::vector<Point2> hull;
    hull.reserve(lower.size() + upper.size() - 2);
    for (size_t i = 0; i + 1 < lower.size(); ++i) {
        hull.push_back(lower[i]);
    }
    for (size_t i = 0; i + 1 < upper.size(); ++i) {
        hull.push_back(upper[i]);
    }

    return ConvexHull(std::move(hull));
}

}  // namespace mulan::math
