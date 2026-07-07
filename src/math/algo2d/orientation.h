/**
 * @file orientation.h
 * @brief 2D 定向谓词 — orientation / toLeft / toLeftOrOn
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::internal::GeometryCore（ToLeftTest），统一至容差系统。
 *
 * 设计：
 *  - orientation(p,q,r)：返回三态枚举（左/右/共线），便于算法区分严格/退化情形。
 *  - toLeft(p,q,r)：r 是否在有向边 p→q 的严格左侧（CCW），即 orientation == Left。
 *  - toLeftOrOn(p,q,r)：左侧或共线（用于凸包 contains）。
 *
 * 容差约定：
 *  - cross = (q-p)×(r-p) 量纲为面积（长度²）。
 *  - 共线阈值取 lengthEps²（默认 1e-18）：|cross| ≤ lengthEps² 视为共线。
 *    这对工程尺度数据仅吸收浮点噪声，不改变几何语义。
 *
 * 注意：
 *  - 这些是符号判定，是凸包/三角剖分/Voronoi 的正确性根基。
 *  - 排序场景（需要严格弱序）不应使用本谓词，应直接用坐标比较。
 */
#pragma once

#include "../linalg/vec2.h"
#include "../geom/point.h"
#include "../scalar/tolerance.h"

namespace mulan::math {

/// 三态定向结果
enum class Orientation {
    Left,      ///< 左转（CCW，cross > lengthEps²）
    Right,     ///< 右转（CW，cross < -lengthEps²）
    Collinear  ///< 共线（|cross| ≤ lengthEps²）
};

/// 三点定向：以 p 为基点，考察有向边 p→q 后 r 的相对位置。
/// cross = (q-p) × (r-p) = (q.x-p.x)(r.y-p.y) - (q.y-p.y)(r.x-p.x)
inline Orientation orientation(const Point2& p, const Point2& q, const Point2& r,
                               const Tolerance& tol = defaultTolerance()) {
    Vec2 pq = q - p;
    Vec2 pr = r - p;
    double cross = pq.cross(pr);
    if (cross > tol.lengthEps * tol.lengthEps) {  // 注意：cross 量纲为面积，用平方 eps
        return Orientation::Left;
    }
    if (cross < -tol.lengthEps * tol.lengthEps) {
        return Orientation::Right;
    }
    return Orientation::Collinear;
}

/// r 是否在 p→q 的严格左侧（CCW）。
inline bool toLeft(const Point2& p, const Point2& q, const Point2& r, const Tolerance& tol = defaultTolerance()) {
    return orientation(p, q, r, tol) == Orientation::Left;
}

/// r 是否在 p→q 的左侧或其上（含共线）。
inline bool toLeftOrOn(const Point2& p, const Point2& q, const Point2& r, const Tolerance& tol = defaultTolerance()) {
    return orientation(p, q, r, tol) != Orientation::Right;
}

/// 向量版 toLeft：u × v > lengthEps²（严格左转）
inline bool toLeft(const Vec2& u, const Vec2& v, const Tolerance& tol = defaultTolerance()) {
    double cross = u.cross(v);
    return cross > tol.lengthEps * tol.lengthEps;
}

}  // namespace mulan::math
