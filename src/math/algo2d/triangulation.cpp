/**
 * @file triangulation.cpp
 * @brief 2D 三角剖分实现 — Triangle2 成员 + 辅助谓词 + Ear Clipping + Bowyer-Watson
 * @author hxxcxx
 * @date 2026-07-07
 */
#include "triangulation.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mulan::math {

// ============================================================
// Triangle2 成员
// ============================================================

double Triangle2::area() const {
    Vec2 e1 = v1 - v0;
    Vec2 e2 = v2 - v0;
    return std::abs(e1.cross(e2)) * 0.5;
}

Point2 Triangle2::circumcenter() const {
    double ax = v0.x, ay = v0.y;
    double bx = v1.x, by = v1.y;
    double cx = v2.x, cy = v2.y;
    double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < 1e-18) {
        // 退化（共线），返回重心作安全默认
        return Point2((ax + bx + cx) / 3.0, (ay + by + cy) / 3.0);
    }
    double a2 = ax * ax + ay * ay;
    double b2 = bx * bx + by * by;
    double c2 = cx * cx + cy * cy;
    double ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    double uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    return Point2(ux, uy);
}

double Triangle2::circumradius() const {
    Point2 cc = circumcenter();
    double r0 = cc.distance(v0);
    double r1 = cc.distance(v1);
    double r2 = cc.distance(v2);
    return std::max({ r0, r1, r2 });
}

bool Triangle2::contains(const Point2& p) const {
    auto cross3 = [](const Point2& a, const Point2& b, const Point2& c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    double c1 = cross3(v0, v1, p);
    double c2 = cross3(v1, v2, p);
    double c3 = cross3(v2, v0, p);
    const double eps = 1e-9;
    bool pos = (c1 > -eps) && (c2 > -eps) && (c3 > -eps);
    bool neg = (c1 < eps) && (c2 < eps) && (c3 < eps);
    return pos || neg;
}

// ============================================================
// TriangulationResult 成员
// ============================================================

double TriangulationResult::totalArea() const {
    double sum = 0.0;
    for (const Triangle2& t : triangles)
        sum += t.area();
    return sum;
}

// ============================================================
// 辅助谓词
// ============================================================
namespace detail {

double signedArea(const std::vector<Point2>& poly) {
    double s = 0.0;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const Point2& a = poly[i];
        const Point2& b = poly[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

bool isCCW(const std::vector<Point2>& poly) {
    return signedArea(poly) > 0.0;
}

bool isConvexCorner(const Point2& prev, const Point2& curr, const Point2& next) {
    Vec2 a = curr - prev;
    Vec2 b = next - curr;
    return a.cross(b) > 0.0;
}

bool pointInTriangle(const Point2& p, const Point2& a, const Point2& b, const Point2& c) {
    auto cr = [](const Point2& x, const Point2& y, const Point2& z) {
        return (y.x - x.x) * (z.y - x.y) - (y.y - x.y) * (z.x - x.x);
    };
    double c1 = cr(a, b, p);
    double c2 = cr(b, c, p);
    double c3 = cr(c, a, p);
    const double eps = 1e-9;
    bool pos = (c1 > -eps) && (c2 > -eps) && (c3 > -eps);
    bool neg = (c1 < eps) && (c2 < eps) && (c3 < eps);
    return pos || neg;
}

void removeCollinear(std::vector<Point2>& poly, const Tolerance& tol) {
    if (poly.size() < 3)
        return;
    std::vector<Point2> out;
    out.reserve(poly.size());
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const Point2& prev = poly[(i + n - 1) % n];
        const Point2& curr = poly[i];
        const Point2& next = poly[(i + 1) % n];
        Vec2 a = curr - prev;
        Vec2 b = next - curr;
        if (std::abs(a.cross(b)) <= tol.lengthEps * tol.lengthEps) {
            continue;  // 共线，丢弃 curr
        }
        out.push_back(curr);
    }
    poly.swap(out);
}

bool inCircle(const Point2& a, const Point2& b, const Point2& c, const Point2& d) {
    double ax = a.x - d.x, ay = a.y - d.y;
    double bx = b.x - d.x, by = b.y - d.y;
    double cx = c.x - d.x, cy = c.y - d.y;
    double ap = ax * ax + ay * ay;
    double bp = bx * bx + by * by;
    double cp = cx * cx + cy * cy;
    double det = ax * (by * cp - bp * cy) - ay * (bx * cp - bp * cx) + ap * (bx * cy - by * cx);
    return det > 0.0;
}

}  // namespace detail

// ============================================================
// 多边形三角剖分（Ear Clipping）O(n²)
// ============================================================

TriangulationResult triangulatePolygon(std::vector<Point2> polygon, const Tolerance& tol) {
    TriangulationResult result;
    if (polygon.size() < 3)
        return result;

    // 规整为 CCW
    if (!detail::isCCW(polygon)) {
        std::reverse(polygon.begin(), polygon.end());
    }
    // 移除共线顶点
    detail::removeCollinear(polygon, tol);
    if (polygon.size() < 3)
        return result;

    // 逐个切耳
    auto isEar = [&](const std::vector<Point2>& poly, size_t i) {
        const size_t n = poly.size();
        size_t prev = (i + n - 1) % n;
        size_t next = (i + 1) % n;
        // 必须是凸角
        if (!detail::isConvexCorner(poly[prev], poly[i], poly[next])) {
            return false;
        }
        // 无其它顶点落在该三角形内
        for (size_t j = 0; j < n; ++j) {
            if (j == prev || j == i || j == next)
                continue;
            if (detail::pointInTriangle(poly[j], poly[prev], poly[i], poly[next])) {
                return false;
            }
        }
        return true;
    };

    std::vector<Point2> poly = polygon;
    while (poly.size() > 3) {
        int earIdx = -1;
        for (size_t i = 0; i < poly.size(); ++i) {
            if (isEar(poly, i)) {
                earIdx = static_cast<int>(i);
                break;
            }
        }
        if (earIdx < 0) {
            // 无耳：多边形可能自相交，中止
            return result;
        }
        size_t n = poly.size();
        size_t prev = (static_cast<size_t>(earIdx) + n - 1) % n;
        size_t next = (static_cast<size_t>(earIdx) + 1) % n;
        result.triangles.emplace_back(poly[prev], poly[earIdx], poly[next]);
        poly.erase(poly.begin() + earIdx);
    }
    // 最后一个三角形
    result.triangles.emplace_back(poly[0], poly[1], poly[2]);
    return result;
}

// ============================================================
// 点集 Delaunay 三角剖分（Bowyer-Watson）O(n²) 最坏
// ============================================================

TriangulationResult triangulateDelaunay(const std::vector<Point2>& points, const Tolerance& tol) {
    TriangulationResult result;
    if (points.size() < 3)
        return result;

    // 计算包围盒，构造超三角形（远大于包围盒，确保所有点落在其内部）
    AABB2 box = AABB2::empty();
    for (const Point2& p : points)
        box.expand(p);
    if (box.isEmpty(tol))
        return result;

    Point2 c = box.center();
    Vec2 sz = box.size();
    // 超三角形边长取包围盒对角线的 20 倍，保证足够大
    double delta = std::max(sz.x, sz.y) * 20.0 + 1.0;
    Point2 st0(c.x - delta, c.y - delta * 0.5);
    Point2 st1(c.x + delta, c.y - delta * 0.5);
    Point2 st2(c.x, c.y + delta);

    std::vector<Triangle2> tris;
    tris.reserve(points.size() * 2);
    tris.emplace_back(st0, st1, st2);

    // 逐点插入
    for (const Point2& p : points) {
        // 找坏三角形（外接圆含 p）
        std::vector<Triangle2> bad;
        std::vector<Triangle2> good;
        for (const Triangle2& t : tris) {
            // 用 inCircle（需 CCW 三角形；Bowyer-Watson 中新三角形均按 CCW 构造，
            // 但保险起见用 inCircle + 方向修正）
            bool in = false;
            if (detail::isCCW({ t.v0, t.v1, t.v2 })) {
                in = detail::inCircle(t.v0, t.v1, t.v2, p);
            } else {
                in = detail::inCircle(t.v0, t.v2, t.v1, p);
            }
            if (in)
                bad.push_back(t);
            else
                good.push_back(t);
        }

        // 收集坏三角形的边，找出"边界边"（只出现一次的边）
        struct Edge {
            Point2 a, b;
        };
        std::vector<Edge> allEdges;
        allEdges.reserve(bad.size() * 3);
        for (const Triangle2& t : bad) {
            allEdges.push_back({ t.v0, t.v1 });
            allEdges.push_back({ t.v1, t.v2 });
            allEdges.push_back({ t.v2, t.v0 });
        }
        // 边等价：端点相同（不考虑方向）。标记出现次数。
        std::vector<bool> shared(allEdges.size(), false);
        for (size_t i = 0; i < allEdges.size(); ++i) {
            for (size_t j = i + 1; j < allEdges.size(); ++j) {
                const Edge& ei = allEdges[i];
                const Edge& ej = allEdges[j];
                bool same = (ei.a.distanceSq(ej.a) <= tol.lengthEps * tol.lengthEps &&
                             ei.b.distanceSq(ej.b) <= tol.lengthEps * tol.lengthEps) ||
                            (ei.a.distanceSq(ej.b) <= tol.lengthEps * tol.lengthEps &&
                             ei.b.distanceSq(ej.a) <= tol.lengthEps * tol.lengthEps);
                if (same) {
                    shared[i] = true;
                    shared[j] = true;
                }
            }
        }
        // 边界边（非共享）与新点构成新三角形
        std::vector<Triangle2> newTris;
        for (size_t i = 0; i < allEdges.size(); ++i) {
            if (shared[i])
                continue;
            const Edge& e = allEdges[i];
            newTris.emplace_back(e.a, e.b, p);
        }

        // good + newTris 形成新三角剖分
        tris.clear();
        tris.insert(tris.end(), good.begin(), good.end());
        tris.insert(tris.end(), newTris.begin(), newTris.end());
    }

    // 移除含超三角形顶点的三角形
    auto isSuperVertex = [&](const Point2& p) {
        return p.distanceSq(st0) <= tol.lengthEps * tol.lengthEps ||
               p.distanceSq(st1) <= tol.lengthEps * tol.lengthEps || p.distanceSq(st2) <= tol.lengthEps * tol.lengthEps;
    };
    for (const Triangle2& t : tris) {
        if (isSuperVertex(t.v0) || isSuperVertex(t.v1) || isSuperVertex(t.v2))
            continue;
        result.triangles.push_back(t);
    }

    return result;
}

}  // namespace mulan::math
