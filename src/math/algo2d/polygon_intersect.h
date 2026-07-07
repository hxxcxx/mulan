/**
 * @file polygon_intersect.h
 * @brief 2D 凸多边形求交 — Sutherland-Hodgman 半平面裁剪
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::ConvexPolygonIntersection，适配至 mulan::math。
 *
 * 提供：
 *  - convexPolygonIntersect(a,b)：两个 CCW 凸多边形的交集，返回顶点（CCW）。
 *  - pointInConvexPolygon(p, poly)：点是否在 CCW 凸多边形内或边界上。
 *
 * 算法：Sutherland-Hodgman 半平面裁剪。
 *   用 b 的每条边（作为有向半平面）依次裁剪 a，保留 a 中落在 b 每条边左侧/其上的部分。
 *   要求 a、b 均为 CCW 凸多边形（本实现以 ConvexHull/顶点数组表示，调用方保证 CCW）。
 *
 * 复杂度说明（勿误导）：
 *   裁剪对 b 的每条边（m 条）遍历当前 a 的全部顶点（至多 n+m），
 *   故为 O(n·m)，而非原代码注释声称的 O(n+m)。真正的 O(n+m) 需 O'Rourke 推进指针法。
 *   原 BeyondConvex 的 "BinarySearch" 变体并非二分搜索（实为边-边枚举 O(n·m) 再按角度排序），
 *   本实现不保留该变体，仅提供正确且诚实的 Sutherland-Hodgman。
 *
 * 退化结果：交集可能退化为点（isPoint）、线段（isSegment）或空（isEmpty）。
 *
 * 容差：点在多边形内的边界判定走 Tolerance（toLeftOrOn，共线阈值 lengthEps²）。
 */
#pragma once

#include "../math_export.h"
#include "../linalg/vec2.h"
#include "../geom/point.h"
#include "convex_hull.h"
#include "orientation.h"
#include "../scalar/tolerance.h"

#include <vector>

namespace mulan::math {

/// 凸多边形求交结果。
struct ConvexIntersectionResult {
    std::vector<Point2> vertices;  ///< 交集多边形顶点（CCW）；退化时为 1 点或 2 点(线段)
    bool isEmpty = true;           ///< 是否无交集
    bool isPoint = false;          ///< 交集退化为单点
    bool isSegment = false;        ///< 交集退化为线段

    /// 转为 ConvexHull（顶点数 < 3 时返回空凸包）。
    ConvexHull toConvexHull() const {
        if (isEmpty || vertices.size() < 3) {
            return ConvexHull{};
        }
        return ConvexHull(vertices);
    }
};

/// 点是否在 CCW 凸多边形内或边界上（toLeftOrOn 判定，O(n)）。
MATH_API bool pointInConvexPolygon(const Point2& p, const std::vector<Point2>& poly,
                                   const Tolerance& tol = defaultTolerance());

namespace detail {

/// 求线段 (s→e) 与有向边 (clipA→clipB) 的交点参数 sa（线段侧）。
/// 返回是否相交于线段内部（含端点）；outPoint 给出交点。
/// 共线/平行返回 false。
MATH_API bool segmentCrossLine(const Point2& s, const Point2& e, const Point2& clipA, const Point2& clipB,
                               Point2* outPoint, const Tolerance& tol);

/// 用有向半平面（保留 clipA→clipB 左侧/其上的点）裁剪一个多边形。
/// Sutherland-Hodgman 单边裁剪。输入 poly 假定为 CCW。
MATH_API std::vector<Point2> clipByHalfPlane(const std::vector<Point2>& poly, const Point2& clipA, const Point2& clipB,
                                             const Tolerance& tol);

}  // namespace detail

/// 两个 CCW 凸多边形的交集（Sutherland-Hodgman 裁剪，O(n·m)）。
///
/// a 以 b 的每条有向边裁剪。返回结果标记退化为点/线段/空。
/// 共线顶点与重复点会被清理。
MATH_API ConvexIntersectionResult convexPolygonIntersect(const std::vector<Point2>& a, const std::vector<Point2>& b,
                                                         const Tolerance& tol = defaultTolerance());

/// ConvexHull 重载（内部取顶点后委托给顶点数组版本）。
MATH_API ConvexIntersectionResult convexPolygonIntersect(const ConvexHull& a, const ConvexHull& b,
                                                         const Tolerance& tol = defaultTolerance());

}  // namespace mulan::math
