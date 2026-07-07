/**
 * @file voronoi.h
 * @brief 2D Voronoi 图 — 增量半平面裁剪法
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::IncrementalVoronoi（半平面裁剪），适配至 mulan::math。
 *
 * 算法：对每个 site，其 Voronoi 单元 = 边界框 ∩ { 与其它所有 site 的垂直平分线
 *   靠近本 site 一侧的半平面 }。逐 site 独立计算，单元多边形用 Sutherland-Hodgman 裁剪。
 *
 * 复杂度（勿误导）：O(n²·k)，其中 k 为单元平均顶点数；最坏 O(n³)。
 *   原 BeyondConvex 注释即写作 "O(n²) ~ O(n³)"。适合中小规模点集；
 *   大规模应改用 Fortune 扫描线 O(n log n)（未实现）。
 *
 * 输出：VoronoiDiagramResult —— 每个 site 一个单元（多边形顶点 + 边），
 *   外加全局去重的边与顶点。本实现不维护拓扑（共享边在每个单元里各存一份，
 *   再在结果层去重），故不输出 DCEL；如需拓扑可对结果另行 buildPolygon。
 *
 * 容差：裁剪的 inside 判定与共线清理走 Tolerance（lengthEps²）。
 */
#pragma once

#include "../math_export.h"
#include "../linalg/vec2.h"
#include "../geom/point.h"
#include "../geom/line.h"  // Segment2
#include "../scalar/tolerance.h"

#include <vector>

namespace mulan::math {

/// Voronoi 单元：单个 site 的势力范围（多边形）。
struct VoronoiCell {
    size_t siteIndex = 0;          ///< 对应输入 site 的下标
    Point2 site{};                 ///< 生成元（site）
    std::vector<Point2> vertices;  ///< 单元多边形顶点（CCW）

    bool isValid() const { return vertices.size() >= 3; }
    size_t vertexCount() const { return vertices.size(); }

    /// 单元边界边（由相邻顶点连成）。
    std::vector<Segment2> edges() const {
        std::vector<Segment2> es;
        const size_t n = vertices.size();
        if (n < 2)
            return es;
        es.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            es.emplace_back(vertices[i], vertices[(i + 1) % n]);
        }
        return es;
    }
};

/// Voronoi 图边界框。
struct VoronoiBounds {
    double minX = -100.0;
    double minY = -100.0;
    double maxX = 100.0;
    double maxY = 100.0;

    VoronoiBounds() = default;
    VoronoiBounds(double minX_, double minY_, double maxX_, double maxY_)
        : minX(minX_), minY(minY_), maxX(maxX_), maxY(maxY_) {}

    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
    bool contains(const Point2& p) const { return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY; }
};

/// Voronoi 图结果：单元集合 + 去重的边/顶点。
struct VoronoiDiagramResult {
    std::vector<Point2> sites;       ///< 输入 site
    std::vector<VoronoiCell> cells;  ///< 每个 site 一个单元
    std::vector<Segment2> edges;     ///< 去重后的所有边
    std::vector<Point2> vertices;    ///< 去重后的所有顶点

    bool isValid() const { return !cells.empty(); }
    size_t cellCount() const { return cells.size(); }
    size_t siteCount() const { return sites.size(); }
};

namespace detail {

/// 点是否在半平面（点 onLine 上，法向 normal 指向保留侧）内或边界上。
/// 保留条件：(p - onLine)·normal >= -eps。
MATH_API bool inHalfPlane(const Point2& p, const Point2& onLine, const Vec2& normal, const Tolerance& tol);

/// 线段 (s→e) 与半平面边界线（onLine 上，法向 normal）的交点。
/// 线参数 t ∈ [0,1]，t = -A·(s-onLine) / (A·(e-s))，A = normal。
/// 注意：仅在 d·normal 跨过 0 时调用（即一端在内一端在外）。
MATH_API Point2 intersectSegHalfPlane(const Point2& s, const Point2& e, const Point2& onLine, const Vec2& normal);

/// 用半平面（onLine, normal）裁剪 CCW 多边形。Sutherland-Hodgman 单边裁剪。
MATH_API std::vector<Point2> clipByHalfPlaneNormal(const std::vector<Point2>& poly, const Point2& onLine,
                                                   const Vec2& normal, const Tolerance& tol);

}  // namespace detail

/// 计算单个 site 的 Voronoi 单元（多边形顶点，CCW）。
/// 起点 = 边界框，逐个用与其它 site 的垂直平分线裁剪。
/// 平分线：midpoint = (a+b)/2，保留 a 一侧的法向 = (a-b)。
MATH_API std::vector<Point2> voronoiCell(const Point2& site, const std::vector<Point2>& allSites, size_t siteIndex,
                                         const VoronoiBounds& bounds, const Tolerance& tol = defaultTolerance());

/// 生成完整 Voronoi 图（增量半平面裁剪，O(n²·k) ~ O(n³)）。
/// 单元数 = site 数；单 site 时整个边界框为一个单元。
MATH_API VoronoiDiagramResult voronoi(const std::vector<Point2>& sites, const VoronoiBounds& bounds = {},
                                      const Tolerance& tol = defaultTolerance());

}  // namespace mulan::math
