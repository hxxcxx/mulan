/**
 * @file polygon_intersect.cpp
 * @brief 2D 凸多边形求交实现 — Sutherland-Hodgman 半平面裁剪
 * @author hxxcxx
 * @date 2026-07-07
 */
#include "polygon_intersect.h"

#include <cmath>
#include <vector>

namespace mulan::math {

bool pointInConvexPolygon(const Point2& p, const std::vector<Point2>& poly, const Tolerance& tol) {
    if (poly.size() < 3)
        return false;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const Point2& a = poly[i];
        const Point2& b = poly[(i + 1) % n];
        if (!toLeftOrOn(a, b, p, tol)) {
            return false;
        }
    }
    return true;
}

namespace detail {

bool segmentCrossLine(const Point2& s, const Point2& e, const Point2& clipA, const Point2& clipB, Point2* outPoint,
                      const Tolerance& tol) {
    Vec2 d = e - s;
    Vec2 clipDir = clipB - clipA;
    double denom = d.cross(clipDir);
    if (std::abs(denom) < 1e-15) {
        return false;  // 平行
    }
    Vec2 sc = clipA - s;
    double sa = sc.cross(clipDir) / denom;
    if (sa < -tol.paramEps || sa > 1.0 + tol.paramEps) {
        return false;
    }
    if (outPoint)
        *outPoint = Point2(s.x + d.x * sa, s.y + d.y * sa);
    return true;
}

std::vector<Point2> clipByHalfPlane(const std::vector<Point2>& poly, const Point2& clipA, const Point2& clipB,
                                    const Tolerance& tol) {
    if (poly.empty())
        return {};
    std::vector<Point2> out;
    out.reserve(poly.size());
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const Point2& cur = poly[i];
        const Point2& nxt = poly[(i + 1) % n];
        bool curIn = toLeftOrOn(clipA, clipB, cur, tol);
        bool nxtIn = toLeftOrOn(clipA, clipB, nxt, tol);
        if (curIn && nxtIn) {
            out.push_back(nxt);
        } else if (curIn && !nxtIn) {
            Point2 hit;
            if (segmentCrossLine(cur, nxt, clipA, clipB, &hit, tol)) {
                out.push_back(hit);
            }
        } else if (!curIn && nxtIn) {
            Point2 hit;
            if (segmentCrossLine(cur, nxt, clipA, clipB, &hit, tol)) {
                out.push_back(hit);
                out.push_back(nxt);
            }
        }
        // 都在外：不输出
    }
    return out;
}

}  // namespace detail

ConvexIntersectionResult convexPolygonIntersect(const std::vector<Point2>& a, const std::vector<Point2>& b,
                                                const Tolerance& tol) {
    ConvexIntersectionResult result;
    if (a.empty() || b.empty()) {
        return result;  // isEmpty=true
    }

    // 先判包含关系（裁剪退化时也能正确处理，但包含是常见快路径）
    if (pointInConvexPolygon(a[0], b, tol)) {
        bool aInB = true;
        for (const Point2& v : a) {
            if (!pointInConvexPolygon(v, b, tol)) {
                aInB = false;
                break;
            }
        }
        if (aInB) {
            result.vertices = a;
            result.isEmpty = false;
            return result;
        }
    }
    if (pointInConvexPolygon(b[0], a, tol)) {
        bool bInA = true;
        for (const Point2& v : b) {
            if (!pointInConvexPolygon(v, a, tol)) {
                bInA = false;
                break;
            }
        }
        if (bInA) {
            result.vertices = b;
            result.isEmpty = false;
            return result;
        }
    }

    // 逐边裁剪
    std::vector<Point2> cur = a;
    for (size_t i = 0; i < b.size() && !cur.empty(); ++i) {
        cur = detail::clipByHalfPlane(cur, b[i], b[(i + 1) % b.size()], tol);
    }
    if (cur.empty()) {
        return result;  // isEmpty=true
    }

    // 清理共线与重复点
    std::vector<Point2> cleaned;
    for (const Point2& p : cur) {
        bool dup = false;
        for (const Point2& q : cleaned) {
            if (p.distanceSq(q) <= tol.lengthEps * tol.lengthEps) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        if (cleaned.size() >= 2) {
            const size_t k = cleaned.size();
            Vec2 v1 = cleaned[k - 1] - cleaned[k - 2];
            Vec2 v2 = p - cleaned[k - 1];
            if (std::abs(v1.cross(v2)) <= tol.lengthEps * tol.lengthEps) {
                cleaned[k - 1] = p;  // 共线：替换末点
                continue;
            }
        }
        cleaned.push_back(p);
    }
    // 环绕首尾共线检查
    if (cleaned.size() >= 3) {
        const size_t k = cleaned.size();
        Vec2 v1 = cleaned[0] - cleaned[k - 1];
        Vec2 v2 = cleaned[1] - cleaned[0];
        if (std::abs(v1.cross(v2)) <= tol.lengthEps * tol.lengthEps) {
            cleaned.erase(cleaned.begin());
        }
    }

    if (cleaned.empty()) {
        return result;
    }
    result.vertices = std::move(cleaned);
    result.isEmpty = false;
    if (result.vertices.size() == 1) {
        result.isPoint = true;
    } else if (result.vertices.size() == 2) {
        result.isSegment = true;
    }
    return result;
}

ConvexIntersectionResult convexPolygonIntersect(const ConvexHull& a, const ConvexHull& b, const Tolerance& tol) {
    return convexPolygonIntersect(a.vertices(), b.vertices(), tol);
}

}  // namespace mulan::math
