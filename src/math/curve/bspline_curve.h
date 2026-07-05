/**
 * @file bspline_curve.h
 * @brief B-spline 曲线（2D / 3D）— 模板化
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学定义（The NURBS Book §3.x）：
 *   C(u) = Σ_{i=0..n} N_{i,p}(u) · P_i
 *   P_i 控制点（n+1 个），p 次数，节点向量 U（长度 n+p+2，非递减）。
 *   定义域：u ∈ [U[p], U[n+1]]。
 *
 * 与 Bezier 的关系：
 *   - Bezier 是 B-spline 的特例（节点向量 = {0..0, 1..1}，无内部节点）。
 *   - B-spline 局部支撑：移动 P_i 只影响 [U[i], U[i+p+1]) 区间。
 *   - clamped 节点向量下，曲线穿过首末控制点（与 Bezier 一致）。
 *
 * 类型设计（同 BezierCurve）：
 *   - 模板参数 Point（Point2 / Point3），Vec 由 Point-Point 自动推导。
 *   - 求值返回 Point，导数返回 Vec。
 *   - de Boor 算法用点的仿射组合（Point + Vec·t 语义）。
 *
 * 参数域与边界：
 *   - u 自动 clamp 到定义域 [U[p], U[m-p]]（m = U.size()-1）。
 *   - 前置条件（次数/控制点/节点向量长度不匹配）用 assert。
 *
 * 算法来源：
 *   de Boor 求值     —— NURBS Book A3.1（局部三角反推，数值稳定）
 *   解析导数         —— NURBS Book A3.4（导数曲线控制点 Q_i = p·(P_{i+1}-P_i)/(U_{i+p+1}-U_{i+1})）
 *                      本实现按需递归求 order 阶，不预算导数曲线节点向量。
 *   节点插入(Boehm) —— NURBS Book A5.1
 */
#pragma once

#include "../basis/bspline_basis.h"
#include "../geom/point.h"

#include <cassert>
#include <utility>
#include <vector>

namespace mulan::math {

template <typename Point>
class BSplineCurveT {
public:
    using PointList = std::vector<Point>;
    using KnotVector = std::vector<double>;
    using Vec = std::decay_t<decltype(std::declval<Point>() - std::declval<Point>())>;

    // ---------- 构造 ----------

    /// 用控制点 + 次数构造，自动生成 clamped 节点向量。
    /// 前置条件：degree ≥ 1，controlPoints.size() > degree。
    BSplineCurveT(int degree, PointList controlPoints)
        : degree_(degree),
          control_points_(std::move(controlPoints)),
          knots_(clampedKnotVector(degree, static_cast<int>(control_points_.size()))) {
        validateInvariants();
    }

    /// 完整构造（控制点 + 显式节点向量）。前置条件：三者长度匹配。
    BSplineCurveT(int degree, PointList controlPoints, KnotVector knots)
        : degree_(degree), control_points_(std::move(controlPoints)), knots_(std::move(knots)) {
        validateInvariants();
    }

    // ---------- 查询 ----------

    int degree() const noexcept { return degree_; }
    int controlPointCount() const noexcept { return static_cast<int>(control_points_.size()); }
    int knotCount() const noexcept { return static_cast<int>(knots_.size()); }

    const PointList& controlPoints() const noexcept { return control_points_; }
    const KnotVector& knots() const noexcept { return knots_; }

    /// 有效参数域 [U[p], U[m-p]]。曲线在此区间内有定义。
    std::pair<double, double> domain() const noexcept {
        const int m = static_cast<int>(knots_.size()) - 1;
        return { knots_[degree_], knots_[m - degree_] };
    }

    /// 结构有效性（节点数 = 控制点数 + 次数 + 1）。
    bool isValid() const noexcept {
        return degree_ >= 1 && !control_points_.empty() &&
               static_cast<int>(knots_.size()) == controlPointCount() + degree_ + 1;
    }

    // ---------- 求值 ----------

    /// De Boor 求值。u 自动 clamp 到定义域。NURBS Book A3.1。
    Point evaluate(double u) const {
        const auto [umin, umax] = domain();
        u = clampToDomain(u, umin, umax);

        const int n = controlPointCount() - 1;
        const int p = degree_;
        const int k = bsplineFindSpan(n, p, u, knots_);

        // d[j] 初始化为相关 p+1 个控制点：d[j] = P_{k-p+j}
        PointList d(p + 1);
        for (int j = 0; j <= p; ++j) {
            d[j] = control_points_[k - p + j];
        }

        // de Boor 递推：r 从 1 到 p
        for (int r = 1; r <= p; ++r) {
            for (int j = p; j >= r; --j) {
                const int idx = k - p + j;
                // alpha = (u - U[idx]) / (U[idx+p+1-r] - U[idx])
                double denom = knots_[idx + p + 1 - r] - knots_[idx];
                double alpha = 0.0;
                if (std::abs(denom) > Tolerance::defaultValue().paramEps) {
                    alpha = (u - knots_[idx]) / denom;
                }
                // d[j] = (1-alpha)·d[j-1] + alpha·d[j] —— 点的仿射组合
                d[j] = mulan::math::lerp(d[j - 1], d[j], alpha);
            }
        }
        return d[p];
    }

    /// 求 order 阶解析导数（NURBS Book A3.4 hodograph 方法）。
    /// 一阶导返回切向量；阶数超过次数时返回零向量。
    /// 前置条件：order ≥ 1。
    Vec derivative(double u, int order = 1) const {
        assert(order >= 1 && "derivative: order must be >= 1");
        const auto [umin, umax] = domain();
        u = clampToDomain(u, umin, umax);

        // NURBS Book A3.4：每次求导构造下一阶导数曲线
        //   导数曲线次数 p' = p - 1
        //   控制点 Q_i = p · (C_{i+1} - C_i) / (U_{i+p+1} - U_{i+1})
        //   节点向量去掉首末各 1 个
        // 第 1 阶导：C_i 是 Point，Q_i 是 Vec；后续阶全部在 Vec 上做。

        // 第一阶：Point → Vec
        if (degree_ < 1)
            return Vec{};
        std::vector<Vec> curCp = derivativeControlPoints(control_points_, knots_, degree_);
        KnotVector curKnots = knots_;
        curKnots.erase(curKnots.begin());
        curKnots.pop_back();
        int curDeg = degree_ - 1;

        // 后续阶：Vec → Vec
        for (int d = 1; d < order; ++d) {
            if (curDeg < 1)
                return Vec{};
            curCp = derivativeControlPoints(curCp, curKnots, curDeg);
            curKnots.erase(curKnots.begin());
            curKnots.pop_back();
            --curDeg;
        }

        return deBoorGeneric(curCp, curKnots, curDeg, u);
    }

    // ---------- 结构操作 ----------

    /// 插入节点 u，重复 multiplicity 次（Boehm 算法，NURBS Book A5.1）。
    /// 原地修改本曲线，返回新的控制点引用。
    /// 前置条件：multiplicity ≥ 1，u 在定义域内。
    void insertKnot(double u, int multiplicity = 1) {
        assert(multiplicity >= 1 && "insertKnot: multiplicity must be >= 1");
        const auto [umin, umax] = domain();
        u = clampToDomain(u, umin, umax);

        const int p = degree_;

        for (int m = 0; m < multiplicity; ++m) {
            const int n = controlPointCount() - 1;
            const int k = bsplineFindSpan(n, p, u, knots_);

            // 计算 u 在当前节点向量中的重数 s（NURBS Book 约定）
            // 新控制点数 = n+2，旧段 P_0..P_{k-p} 与 P_{k-s}..P_n 保留，
            // 中间 Q_{k-p+1}..Q_{k-s} 由仿射组合产生（p-s 个）。
            int s = 0;
            for (size_t i = 0; i < knots_.size(); ++i) {
                if (std::abs(knots_[i] - u) <= Tolerance::defaultValue().paramEps)
                    ++s;
            }
            // 注：s 是全局重数；NURBS Book 用的是 span 内重数，但新点计数公式
            //     (k-p+1) + (p-s_new) + ... 这里 s_new 取当前重数即可让总数正确。

            PointList nw;
            nw.reserve(control_points_.size() + 1);
            // 前段不变：P_0..P_{k-p}
            for (int i = 0; i <= k - p; ++i)
                nw.push_back(control_points_[i]);
            // 中间新点：Q_i = (1-a)·P_{i-1} + a·P_i
            for (int i = k - p + 1; i <= k - s; ++i) {
                double denom = knots_[i + p] - knots_[i];
                double a = std::abs(denom) > Tolerance::defaultValue().paramEps ? (u - knots_[i]) / denom : 0.0;
                nw.push_back(mulan::math::lerp(control_points_[i - 1], control_points_[i], a));
            }
            // 后段不变：P_{k-s}..P_n
            for (int i = k - s; i <= n; ++i)
                nw.push_back(control_points_[i]);

            // 插入节点
            knots_.insert(knots_.begin() + k + 1, u);
            control_points_ = std::move(nw);
        }
    }

private:
    int degree_;
    PointList control_points_;
    KnotVector knots_;

    void validateInvariants() const {
        assert(degree_ >= 1 && "BSplineCurve: degree must be >= 1");
        assert(controlPointCount() > degree_ && "BSplineCurve: control point count must be > degree");
        assert(knotCount() == controlPointCount() + degree_ + 1 &&
               "BSplineCurve: knot count must equal controlPointCount + degree + 1");
    }

    static double clampToDomain(double u, double umin, double umax) noexcept {
        if (u < umin)
            return umin;
        if (u > umax)
            return umax;
        return u;
    }

    // ---- 通用 de Boor 求值：对 Point 或 Vec 控制点都适用 ----
    template <typename T>
    static T deBoorGeneric(const std::vector<T>& cp, const KnotVector& U, int p, double u) {
        const int n = static_cast<int>(cp.size()) - 1;
        const int k = bsplineFindSpan(n, p, u, U);
        std::vector<T> d(p + 1);
        for (int j = 0; j <= p; ++j)
            d[j] = cp[k - p + j];
        for (int r = 1; r <= p; ++r) {
            for (int j = p; j >= r; --j) {
                const int idx = k - p + j;
                double denom = U[idx + p + 1 - r] - U[idx];
                double alpha = 0.0;
                if (std::abs(denom) > Tolerance::defaultValue().paramEps) {
                    alpha = (u - U[idx]) / denom;
                }
                d[j] = lerpT(d[j - 1], d[j], alpha);
            }
        }
        return d[p];
    }

    // ---- NURBS Book A3.4 导数曲线控制点 ----
    // 入参 C_i 类型可以是 Point 或 Vec；出参恒为 Vec（差分 + 缩放）。
    template <typename T>
    static std::vector<Vec> derivativeControlPoints(const std::vector<T>& cp, const KnotVector& U, int p) {
        std::vector<Vec> q;
        q.reserve(cp.size() - 1);
        for (size_t i = 0; i + 1 < cp.size(); ++i) {
            double denom = U[i + p + 1] - U[i + 1];
            double scale = std::abs(denom) > Tolerance::defaultValue().paramEps ? static_cast<double>(p) / denom : 0.0;
            q.push_back(scale * (cp[i + 1] - cp[i]));
        }
        return q;
    }

    // ---- 通用 lerp：Point 用 mulan::math::lerp（仿射），Vec 用向量插值 ----
    static Point lerpT(const Point& a, const Point& b, double t) { return mulan::math::lerp(a, b, t); }
    static Vec lerpT(const Vec& a, const Vec& b, double t) { return a + (b - a) * t; }
};

// ============================================================
// 别名
// ============================================================

using BSplineCurve2d = BSplineCurveT<Point2>;
using BSplineCurve3d = BSplineCurveT<Point3>;

}  // namespace mulan::math
