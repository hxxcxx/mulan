/**
 * @file voronoi.cpp
 * @brief 2D Voronoi 图实现 — 增量半平面裁剪法
 * @author hxxcxx
 * @date 2026-07-07
 */
#include "voronoi.h"

#include <vector>

namespace mulan::math {

namespace detail {

bool inHalfPlane(const Point2& p, const Point2& onLine, const Vec2& normal, const Tolerance& tol) {
    Vec2 d = p - onLine;
    return d.dot(normal) >= -tol.lengthEps;
}

Point2 intersectSegHalfPlane(const Point2& s, const Point2& e, const Point2& onLine, const Vec2& normal) {
    Vec2 d = e - s;
    double denom = normal.dot(d);
    Vec2 ws = s - onLine;
    double t = -normal.dot(ws) / denom;
    return Point2(s.x + d.x * t, s.y + d.y * t);
}

std::vector<Point2> clipByHalfPlaneNormal(const std::vector<Point2>& poly, const Point2& onLine, const Vec2& normal,
                                          const Tolerance& tol) {
    if (poly.empty())
        return {};
    std::vector<Point2> out;
    out.reserve(poly.size());
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const Point2& cur = poly[i];
        const Point2& nxt = poly[(i + 1) % n];
        bool curIn = inHalfPlane(cur, onLine, normal, tol);
        bool nxtIn = inHalfPlane(nxt, onLine, normal, tol);
        if (curIn && nxtIn) {
            out.push_back(nxt);
        } else if (curIn && !nxtIn) {
            out.push_back(intersectSegHalfPlane(cur, nxt, onLine, normal));
        } else if (!curIn && nxtIn) {
            out.push_back(intersectSegHalfPlane(cur, nxt, onLine, normal));
            out.push_back(nxt);
        }
        // 都在外：不输出
    }
    return out;
}

}  // namespace detail

std::vector<Point2> voronoiCell(const Point2& site, const std::vector<Point2>& allSites, size_t siteIndex,
                                const VoronoiBounds& bounds, const Tolerance& tol) {
    // 起点：边界框矩形（CCW）
    std::vector<Point2> cell = {
        Point2(bounds.minX, bounds.minY),
        Point2(bounds.maxX, bounds.minY),
        Point2(bounds.maxX, bounds.maxY),
        Point2(bounds.minX, bounds.maxY),
    };
    for (size_t j = 0; j < allSites.size(); ++j) {
        if (j == siteIndex)
            continue;
        Point2 mid((site.x + allSites[j].x) * 0.5, (site.y + allSites[j].y) * 0.5);
        Vec2 normal(site - allSites[j]);  // 指向 site 一侧
        if (normal.lengthSq() <= tol.lengthEps * tol.lengthEps) {
            continue;                     // 重合 site，跳过
        }
        cell = detail::clipByHalfPlaneNormal(cell, mid, normal, tol);
        if (cell.empty())
            break;
    }
    return cell;
}

VoronoiDiagramResult voronoi(const std::vector<Point2>& sites, const VoronoiBounds& bounds, const Tolerance& tol) {
    VoronoiDiagramResult result;
    if (sites.empty())
        return result;
    result.sites = sites;

    for (size_t i = 0; i < sites.size(); ++i) {
        VoronoiCell cell;
        cell.siteIndex = i;
        cell.site = sites[i];
        cell.vertices = voronoiCell(sites[i], sites, i, bounds, tol);
        result.cells.push_back(std::move(cell));
    }

    // 去重收集边（无向比较：两端点距离在容差内视为同边）
    auto edgeSeen = [](const std::vector<Segment2>& es, const Segment2& e, double eps2) {
        for (const Segment2& s : es) {
            bool same = (s.start.distanceSq(e.start) <= eps2 && s.end.distanceSq(e.end) <= eps2) ||
                        (s.start.distanceSq(e.end) <= eps2 && s.end.distanceSq(e.start) <= eps2);
            if (same)
                return true;
        }
        return false;
    };
    const double eps2 = tol.lengthEps * tol.lengthEps;
    for (const VoronoiCell& c : result.cells) {
        for (const Segment2& e : c.edges()) {
            if (!edgeSeen(result.edges, e, eps2)) {
                result.edges.push_back(e);
            }
        }
    }

    // 去重收集顶点
    auto vertSeen = [&eps2](const std::vector<Point2>& vs, const Point2& p) {
        for (const Point2& v : vs) {
            if (v.distanceSq(p) <= eps2)
                return true;
        }
        return false;
    };
    for (const VoronoiCell& c : result.cells) {
        for (const Point2& v : c.vertices) {
            if (!vertSeen(result.vertices, v)) {
                result.vertices.push_back(v);
            }
        }
    }

    return result;
}

}  // namespace mulan::math
