/**
 * @file convex_hull.h
 * @brief 2D 凸包 — ConvexHull 类型 + 三种构造算法（自由函数入口，header-only）
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::ConvexHull / ConvexHullBuilder / ConvexHullFactory。
 * 适配要点：
 *  - 命名空间 geometry:: → mulan::math
 *  - 类型 Point2D/Vector2D/Edge2D → Point2/Vec2/Segment2（复用 math 既有类型）
 *  - 命名 PascalCase → snake_case，成员方法对齐 math 风格（length/area/...）
 *  - 容差 1e-9 硬编码 → Tolerance 参数；排序仍用坐标严格比较（保证严格弱序）
 *  - 工厂 ConvexHullFactory::Create → 自由函数 convexHull(...)，对齐 intersect() 风格
 *  - 形态：header-only（实现 inline）。math 模块保持 INTERFACE 约定。
 *
 * ConvexHull 约定：顶点按逆时针（CCW）顺序存储。
 *
 * 算法：
 *  - JarvisMarch（礼品包扎）  O(nh)，h 为凸包顶点数
 *  - GrahamScan（极角排序）    O(n log n)
 *  - MonotoneChain（单调链）   O(n log n)，默认推荐
 *
 * 容差化策略（关键正确性要点）：
 *  - 排序谓词一律用坐标的裸比较（<），保证严格弱序。容差不适用于排序。
 *  - 符号判定（弹栈/共线去重）走 Tolerance：
 *      MonotoneChain 弹栈用 toLeft 严格左转判定（原 cross <= 0 弹，
 *        等价于"非严格左转即弹"，即保留严格凸的顶点）；
 *      Graham 共线去重用 orientation() 三态。
 *  - "共线时保留更远/更近点"的原始语义保持不变。
 */
#pragma once

#include "../math_export.h"
#include "../linalg/vec2.h"
#include "../geom/point.h"
#include "../geom/line.h"  // Segment2
#include "../scalar/tolerance.h"
#include "orientation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace mulan::math {

/// 凸包构造算法
enum class ConvexHullAlgorithm {
    MonotoneChain,  ///< 单调链 O(n log n)（默认）
    GrahamScan,     ///< 极角排序 O(n log n)
    JarvisMarch,    ///< 礼品包扎 O(nh)
};

/// 2D 凸包：CCW 顺序的凸多边形顶点。
///
/// 顶点存储为值类型 Point2；不持有外部点集引用。
/// 空凸包（顶点数 < 3）的 contains/area 等查询返回安全默认值。
class ConvexHull {
public:
    ConvexHull() = default;

    /// 由已按 CCW 排序的顶点构造（调用方负责顺序，不重新构造凸包）。
    explicit ConvexHull(std::vector<Point2> vertices) : vertices_(std::move(vertices)) {}

    /// 顶点（CCW 顺序）
    const std::vector<Point2>& vertices() const { return vertices_; }
    /// 顶点数
    size_t size() const { return vertices_.size(); }
    /// 是否为空（无顶点）
    bool isEmpty() const { return vertices_.empty(); }

    /// 带环绕的顶点访问（index 超出范围自动取模）
    const Point2& vertexAt(size_t index) const { return vertices_[index % vertices_.size()]; }
    /// CCW 方向的下一个顶点
    const Point2& next(size_t index) const {
        if (vertices_.empty())
            return vertices_[0];  // UB guard，实际不应发生
        return vertices_[(index + 1) % vertices_.size()];
    }
    /// CW 方向的上一个顶点
    const Point2& prev(size_t index) const {
        if (vertices_.empty())
            return vertices_[0];
        return vertices_[(index + vertices_.size() - 1) % vertices_.size()];
    }

    /// 点是否在凸包内或边界上（基于 toLeft，对 CCW 凸包：所有边左侧或其上）。
    MATH_API bool contains(const Point2& p, const Tolerance& tol = defaultTolerance()) const;

    /// 凸包的所有边（CCW），返回 Segment2 数组。
    MATH_API std::vector<Segment2> edges() const;

    /// 凸包面积（鞋带公式，绝对值）。
    MATH_API double area() const;

    /// 凸包周长。
    MATH_API double perimeter() const;

private:
    std::vector<Point2> vertices_;  ///< CCW 顺序顶点
};

// ============================================================
// 构造入口（自由函数）
// ============================================================

/// 由点集构造凸包，使用指定算法与容差。
/// 少于 3 个点时返回空凸包。
MATH_API ConvexHull convexHull(const std::vector<Point2>& points,
                               ConvexHullAlgorithm algo = ConvexHullAlgorithm::MonotoneChain,
                               const Tolerance& tol = defaultTolerance());

/// Jarvis March（礼品包扎），O(nh)。
MATH_API ConvexHull convexHullJarvisMarch(const std::vector<Point2>& points, const Tolerance& tol = defaultTolerance());

/// Graham Scan（极角排序），O(n log n)。
MATH_API ConvexHull convexHullGrahamScan(const std::vector<Point2>& points, const Tolerance& tol = defaultTolerance());

/// Monotone Chain（单调链），O(n log n)。
MATH_API ConvexHull convexHullMonotoneChain(const std::vector<Point2>& points,
                                            const Tolerance& tol = defaultTolerance());

// ============================================================
// 内部辅助
// ============================================================
namespace detail {

/// 找最左下点（最小 x，x 相同取最小 y）。
/// 注意：用裸坐标比较保证严格弱序，不引入容差。
MATH_API int findLeftmostLowest(const std::vector<Point2>& points);

/// 找最下点（最小 y，y 相同取最小 x）。
MATH_API int findLowest(const std::vector<Point2>& points);

/// 从 origin 看去，candidate 是否比 currentBest "更逆时针"。
/// 返回 true 表示 candidate 应取代 currentBest。
/// - 若 origin→candidate→currentBest 为左转（candidate 更靠左），取 candidate。
/// - 若三者共线（candidate 与 currentBest 同向），取更远者（保证凸包边端点）。
MATH_API bool isMoreCounterClockwise(const Point2& origin, const Point2& candidate, const Point2& currentBest,
                                     const Tolerance& tol);

/// 以 origin 为极角基准比较 a、b（严格弱序，用于排序）。
/// 返回 true 表示 a 应排在 b 之前。
///
/// 前置条件：origin 是点集的最低点（最小 y，平局取最小 x），
///   故其余点相对 origin 均位于闭上半平面（y≥0），极角 ∈ [0,π]。
///   这是 Graham Scan 选最低点作 pivot 的标准做法。
///
/// 排序规则：
///  - 不同向：叉积 oa×ob > 0 ⟹ a 的极角更小（a 排前）。
///  - 共线（同向或反向）：近的排前（保证扫描时先处理近点）。
///
/// 注意：此谓词必须满足严格弱序，故共线判定用硬阈值而非容差；
/// 极角排序对容差不敏感，1e-12 是数值安全的工程阈值。
///
/// 修正记录：原 BeyondConvex 实现以"上半平面(y>0)"分桶，会把
///   y==0 的边界点（如 (1,0)，极角为 0）误判为下半平面而排到后面，
///   导致顶点顺序错误（实测正方形得到自相交结果）。改为纯叉积比较，
///   在 origin 为最低点的前提下既正确又更简洁。
inline bool compareByPolarAngle(const Point2& origin, const Point2& a, const Point2& b) {
    Vec2 oa = a - origin;
    Vec2 ob = b - origin;
    double cross = oa.cross(ob);
    if (std::abs(cross) > 1e-12) {
        return cross > 0.0;                // a 极角更小
    }
    return oa.lengthSq() < ob.lengthSq();  // 共线：近的在前
}

/// 坐标字典序比较（严格弱序，用于排序）。
inline bool lexicoLess(const Point2& a, const Point2& b) {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
}

/// 单调链弹栈：v1×v2 非严格左转即弹栈（保留严格凸的顶点）。
///
/// 阈值含义：
///   cross = v1×v2 量纲为面积（长度²）。
///   原实现用裸 cross <= 0；这里改用 cross <= lengthEps² 作零判定，
///   其中 lengthEps² = (1e-9)² = 1e-18，对工程尺度数据等价于数值零，
///   仅吸收浮点噪声，不改变几何语义。
///   cross <= 1e-18 ⟹ 右转或共线 ⟹ 弹栈；仅严格左转（cross > 1e-18）保留。
MATH_API void buildHalfHull(std::vector<Point2>& half, const Point2& p, const Tolerance& tol);

}  // namespace detail

}  // namespace mulan::math
