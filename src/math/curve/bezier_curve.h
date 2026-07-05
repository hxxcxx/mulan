/**
 * @file bezier_curve.h
 * @brief Bezier 曲线（2D / 3D）— 模板化
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学定义：
 *   n 次 Bezier 曲线 B(t) = Σ_{i=0..n} B_{i,n}(t) · P_i，t ∈ [0,1]
 *   P_i 为控制点，B_{i,n} 为 Bernstein 多项式（见 bernstein.h）。
 *
 * 类型设计：
 *   - 模板参数 Point 由调用方指定（Point2 / Point3），控制点是【位置】。
 *   - Vec 类型由 Point - Point 自动推导（decltype），用于导数返回类型。
 *     Point3 → Vec3，Point2 → Vec2，无需手写 trait。
 *   - De Casteljau 求值用 mulan::math::lerp(Point, Point, t)：点的仿射组合
 *     （Point + (Point-Point)·t），全程符合 point.h 的仿射语义。
 *   - 降阶算法需在向量空间内做线性运算（Point 不支持 Point·scalar），
 *     实现时以 P_0 为基准平移到向量空间，求解后加回。
 *
 * 参数域与边界：
 *   - 求值/导数/细分接口统一 clamp t 到 [0,1]；越界不报错，行为可预测。
 *   - 输入类前置条件（空控制点、导数阶越界、对 0 阶曲线降阶等）用 assert：
 *     这些是调用方 bug，不是可恢复的运行时错误，符合 math 库"无 optional/expected"的约定。
 *
 * 算法来源：
 *   De Casteljau / 升阶 / 降阶 / 细分 —— 经典 CAGD 算法（参考 The NURBS Book §3.1-3.4，
 *   或 Farin "Curves and Surfaces for CAGD"）。降阶用前向/后向解平均，是有损近似。
 */
#pragma once

#include "../basis/bernstein.h"
#include "../geom/point.h"

#include <cassert>
#include <utility>
#include <vector>

namespace mulan::math {

// ============================================================
// BezierCurve —— 模板化（Point = Point2 | Point3）
// ============================================================

template<typename Point>
class BezierCurveT {
public:
    using PointList = std::vector<Point>;
    /// 由 Point - Point 自动推导对应向量类型（Point3→Vec3，Point2→Vec2）
    using Vec = std::decay_t<decltype(std::declval<Point>() - std::declval<Point>())>;

    // ---------- 构造 ----------

    /// 用控制点构造。前置条件：points 非空。
    explicit BezierCurveT(PointList points)
        : control_points_(std::move(points)) {
        assert(!control_points_.empty() && "BezierCurve: control points must not be empty");
    }

    // ---------- 查询 ----------

    /// 曲线次数 = 控制点数 - 1。0 阶 = 单个控制点（退化为常值曲线）。
    int degree() const noexcept {
        return static_cast<int>(control_points_.size()) - 1;
    }

    int controlPointCount() const noexcept {
        return static_cast<int>(control_points_.size());
    }

    const PointList& controlPoints() const noexcept { return control_points_; }

    Point&       controlPoint(int idx)       { return control_points_[idx]; }
    const Point& controlPoint(int idx) const { return control_points_[idx]; }

    // ---------- 求值 ----------

    /// 直接用 Bernstein 基求值。t 自动 clamp 到 [0,1]。
    /// 注：n 较大时直接求值不如 De Casteljau 稳定，优先用 deCasteljau。
    Point evaluate(double t) const {
        t = clampT(t);
        const int n = degree();
        // B(t) 是点的仿射组合（Σ B_{i,n} = 1）。以 P_0 为基准累加位移，
        // 避免依赖 Point + Point（point.h 故意不提供）。
        Vec acc{}; // 零向量
        for (int i = 1; i <= n; ++i) {
            acc += bernstein(i, n, t) * (control_points_[i] - control_points_[0]);
        }
        return control_points_[0] + acc;
    }

    /// De Casteljau 求值（数值更稳定，推荐）。t 自动 clamp。
    Point deCasteljau(double t) const {
        t = clampT(t);
        PointList work = control_points_;
        const int n = degree();
        for (int k = 1; k <= n; ++k) {
            for (int i = 0; i <= n - k; ++i) {
                work[i] = lerp(work[i], work[i + 1], t); // 点的仿射组合
            }
        }
        return work[0];
    }

    /// 求 order 阶导数在 t 处的值。返回【向量】（位移语义，如切向/法向）。
    /// 前置条件：1 ≤ order ≤ degree。
    Vec derivative(double t, int order = 1) const {
        assert(order >= 1 && order <= degree() && "derivative: order out of range [1, degree]");
        t = clampT(t);

        // 1) 对控制多边形取 order 次差分，得到导数曲线的向量控制点。
        //    每求一阶：m 个向量 → (m-1) 个向量 = v_{i+1} - v_i。
        //    初始向量 = P_{i+1} - P_i（Point - Point = Vec）。
        std::vector<Vec> deriv;
        deriv.reserve(control_points_.size());
        for (size_t i = 0; i + 1 < control_points_.size(); ++i) {
            deriv.push_back(control_points_[i + 1] - control_points_[i]);
        }
        for (int d = 1; d < order; ++d) {
            std::vector<Vec> next;
            next.reserve(deriv.size() > 1 ? deriv.size() - 1 : 0);
            for (size_t i = 0; i + 1 < deriv.size(); ++i) {
                next.push_back(deriv[i + 1] - deriv[i]);
            }
            deriv = std::move(next);
        }

        // 2) 缩放因子 ∏_{k=0}^{order-1} (n - k)（Bezier Hodograph 公式）
        double scale = 1.0;
        for (int k = 0; k < order; ++k) scale *= static_cast<double>(degree() - k);
        for (auto& v : deriv) v = v * scale;

        // 3) 对向量控制点跑 De Casteljau（向量空间线性组合）
        const int deg = static_cast<int>(deriv.size()) - 1;
        for (int k = 1; k <= deg; ++k) {
            for (int i = 0; i <= deg - k; ++i) {
                deriv[i] = deriv[i] + (deriv[i + 1] - deriv[i]) * t;
            }
        }
        return deriv[0];
    }

    // ---------- 结构操作 ----------

    /// 在 t 处细分，返回 [0,t] 与 [t,1] 两条 Bezier 曲线。t 自动 clamp。
    /// 算法：构造 De Casteljau 三角，取每层首点为左曲线、末点（逆序）为右曲线。
    std::pair<BezierCurveT, BezierCurveT> subdivide(double t) const {
        t = clampT(t);
        const int n = degree();

        std::vector<PointList> pyramid(n + 1);
        pyramid[0] = control_points_;
        for (int k = 1; k <= n; ++k) {
            pyramid[k].resize(n + 1 - k);
            for (int i = 0; i <= n - k; ++i) {
                pyramid[k][i] = lerp(pyramid[k - 1][i], pyramid[k - 1][i + 1], t);
            }
        }

        PointList left, right;
        left.reserve(n + 1);
        right.reserve(n + 1);
        for (int k = 0; k <= n; ++k) {
            left.push_back(pyramid[k][0]);       // 每层首点
            right.push_back(pyramid[n - k][k]);  // 每层末点（下标 k 即该层最后一个）
        }
        return {BezierCurveT(std::move(left)), BezierCurveT(std::move(right))};
    }

    /// 升阶：升 1 次。精确表示（同一条曲线，控制点多 1 个）。
    BezierCurveT elevateDegree() const {
        const int n = degree();
        PointList nw;
        nw.reserve(n + 2);

        nw.push_back(control_points_[0]);
        for (int i = 1; i <= n; ++i) {
            // P*_i = (i/(n+1))·P_{i-1} + (1 - i/(n+1))·P_i  —— 点的仿射组合
            // lerp(P_i, P_{i-1}, a) = P_i + a·(P_{i-1} - P_i) = (1-a)·P_i + a·P_{i-1}
            double a = static_cast<double>(i) / (n + 1);
            nw.push_back(lerp(control_points_[i], control_points_[i - 1], a));
        }
        nw.push_back(control_points_[n]);
        return BezierCurveT(std::move(nw));
    }

    /// 降阶：降 1 次。有损近似（前向解保左端、后向解保右端，取平均）。
    /// 前置条件：degree ≥ 1。
    BezierCurveT reduceDegree() const {
        const int n = degree();
        assert(n >= 1 && "reduceDegree: degree must be >= 1");

        // 以 P_0 为基准平移到向量空间（Point 不支持 Point·scalar，但 Vec 支持）
        const Point origin = control_points_[0];
        std::vector<Vec> cp(n + 1);
        for (int i = 0; i <= n; ++i) cp[i] = control_points_[i] - origin;

        // 前向解（保左端）：fwd[0] = cp[0]；fwd[i] = (n·cp[i] - i·fwd[i-1]) / (n - i)
        std::vector<Vec> fwd(n);
        fwd[0] = cp[0];
        for (int i = 1; i < n; ++i) {
            fwd[i] = (static_cast<double>(n) * cp[i]
                      - static_cast<double>(i) * fwd[i - 1])
                     / static_cast<double>(n - i);
        }
        // 后向解（保右端）：bwd[n-1] = cp[n]；bwd[i] = (n·cp[i+1] - (n-i-1)·bwd[i+1]) / (i+1)
        std::vector<Vec> bwd(n);
        bwd[n - 1] = cp[n];
        for (int i = n - 2; i >= 0; --i) {
            bwd[i] = (static_cast<double>(n) * cp[i + 1]
                      - static_cast<double>(n - i - 1) * bwd[i + 1])
                     / static_cast<double>(i + 1);
        }
        // 平均并平移回点空间
        PointList result;
        result.reserve(n);
        for (int i = 0; i < n; ++i) {
            result.push_back(origin + (fwd[i] + bwd[i]) * 0.5);
        }
        return BezierCurveT(std::move(result));
    }

private:
    PointList control_points_;

    /// 将 t clamp 到 [0,1]（Bezier 参数域）
    static double clampT(double t) noexcept {
        return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    }
};

// ============================================================
// 别名
// ============================================================

using BezierCurve2d = BezierCurveT<Point2>;
using BezierCurve3d = BezierCurveT<Point3>;

} // namespace mulan::math
