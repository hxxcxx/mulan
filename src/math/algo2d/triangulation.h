/**
 * @file triangulation.h
 * @brief 2D 三角剖分 — 多边形 Ear Clipping + 点集 Delaunay(Bowyer-Watson)
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::triangulation（EarClipping / Delaunay）。
 *
 * 提供两个独立入口（按输入语义区分）：
 *  - triangulatePolygon(polygon)：对简单多边形（带序顶点）做 Ear Clipping，O(n²)。
 *  - triangulateDelaunay(points)：对无序点集做 Delaunay 三角剖分（Bowyer-Watson），O(n²) 最坏。
 *
 * 适配说明：
 *  - Delaunay 原 BeyondConvex 实现强耦合 DCEL（Face / HalfEdge 指针拓扑）。本实现改写为
 *    基于普通 std::vector<Triangle2> 的独立 Bowyer-Watson，不依赖 DCEL，
 *    更易维护、更易测试；数值逻辑与原版一致（超三角形 + 坏三角形删除 + 重新连接）。
 *  - 容差化：退化（零面积）判定、inCircle 判定走 inCircle() 谓词与 lengthEps²。
 *
 * Triangle 类型提供 area / circumcenter / circumradius / contains / isDegenerate。
 *
 * 复杂度（勿误导）：
 *  - Ear Clipping：O(n²)。
 *  - Bowyer-Watson：最坏 O(n²)（无空间索引）；平均 O(n log n) 需随机化 + 空间索引。
 */
#pragma once

#include "../math_export.h"
#include "../linalg/vec2.h"
#include "../geom/point.h"
#include "../geom/aabb.h"
#include "orientation.h"
#include "../scalar/tolerance.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mulan::math {

/// 三角形（3 个顶点）。无序，但 area/circumcenter 等与顶点顺序无关（取绝对值）。
struct Triangle2 {
    Point2 v0{};
    Point2 v1{};
    Point2 v2{};

    constexpr Triangle2() = default;
    constexpr Triangle2(const Point2& a, const Point2& b, const Point2& c) : v0(a), v1(b), v2(c) {}

    /// 面积（绝对值）。
    MATH_API double area() const;

    /// 是否退化（面积近零）。
    bool isDegenerate(const Tolerance& tol = defaultTolerance()) const {
        return area() <= tol.lengthEps * tol.lengthEps;
    }

    /// 外心。
    MATH_API Point2 circumcenter() const;

    /// 外接圆半径。
    MATH_API double circumradius() const;

    /// 点是否在三角形内或边界上（重心坐标同号判定）。
    MATH_API bool contains(const Point2& p) const;
};

/// 三角剖分结果。
struct TriangulationResult {
    std::vector<Triangle2> triangles;  ///< 生成的三角形

    bool isValid() const { return !triangles.empty(); }
    size_t triangleCount() const { return triangles.size(); }

    /// 所有三角形面积之和。
    MATH_API double totalArea() const;
};

// ============================================================
// 辅助谓词（内部）
// ============================================================
namespace detail {

/// 多边形有符号面积（CCW 为正）。鞋带公式：
///   Σ (xi·yj - xj·yi) / 2，CCW 顶点序为正。
MATH_API double signedArea(const std::vector<Point2>& poly);

/// 是否 CCW（signedArea > 0）。
MATH_API bool isCCW(const std::vector<Point2>& poly);

/// (prev,curr,next) 是否构成 CCW 凸角（严格左转）。
MATH_API bool isConvexCorner(const Point2& prev, const Point2& curr, const Point2& next);

/// 点是否在三角形内或边界（与 Triangle2::contains 同逻辑，独立提供便于 ear clipping）。
MATH_API bool pointInTriangle(const Point2& p, const Point2& a, const Point2& b, const Point2& c);

/// 移除多边形上的共线顶点（在弹栈 ear clipping 前清理，避免退化三角形）。
/// 使用 cross ≈ 0（容差 lengthEps²）判定共线。
MATH_API void removeCollinear(std::vector<Point2>& poly, const Tolerance& tol);

/// inCircle 判定：点 d 是否在 △abc 的外接圆内部（严格）。
/// 使用行列式（incircle determinant）：
///   | ax-dx  ay-dy  (ax-dx)²+(ay-dy)² |
///   | bx-dx  by-dy  ...                | > 0
///   | cx-dx  cy-dy  ...                |
/// 当 abc 为 CCW 时，行列式 > 0 ⟺ d 严格在内。
MATH_API bool inCircle(const Point2& a, const Point2& b, const Point2& c, const Point2& d);

}  // namespace detail

// ============================================================
// 多边形三角剖分（Ear Clipping）O(n²)
// ============================================================

/// 对简单多边形做 Ear Clipping 三角剖分。
///
/// 输入：多边形顶点（CCW 或 CW 均可，内部会规整为 CCW）。
/// 要求：简单多边形（无自相交）；内部会移除共线顶点。
/// 输出：三角形数组。n 个顶点 → n-2 个三角形。
MATH_API TriangulationResult triangulatePolygon(std::vector<Point2> polygon, const Tolerance& tol = defaultTolerance());

// ============================================================
// 点集 Delaunay 三角剖分（Bowyer-Watson）O(n²) 最坏
// ============================================================

/// 对无序点集做 Delaunay 三角剖分（Bowyer-Watson）。
///
/// 算法：
///  1. 构造一个包含所有点的超三角形。
///  2. 逐点插入：找到外接圆包含该点的"坏三角形"，删除它们，
///     对坏三角形集合的边界边与新点连接形成新三角形。
///  3. 移除所有与超三角形顶点相连的三角形。
///
/// 输出三角形以原始输入点为顶点（不含超三角形顶点）。
MATH_API TriangulationResult triangulateDelaunay(const std::vector<Point2>& points,
                                                 const Tolerance& tol = defaultTolerance());

}  // namespace mulan::math
