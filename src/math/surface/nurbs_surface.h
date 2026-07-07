/**
 * @file nurbs_surface.h
 * @brief NURBS 曲面（张量积）— 仅数学
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学定义（The NURBS Book §4.x）：
 *   S(u,v) = Σ_i Σ_j N_{i,p}(u)·N_{j,q}(v)·w_{ij}·P_{ij}
 *          / Σ_i Σ_j N_{i,p}(u)·N_{j,q}(v)·w_{ij}
 *   齐次坐标求值：P_{ij} 提升为 (w·P, w)（4D），张量积 de Boor，末分量除。
 *
 * 与 BSplineSurface 的关系：权重全 1 时退化为 B-spline 曲面。
 *
 * 类型设计：同 BSplineSurface（控制点 Point3，导数/法向 Vec3），
 *          额外的权重网格 weights_[j][i] 与控制点网格同形。
 *
 * 算法来源：齐次 de Boor 张量积 + NURBS Book §4.4 有理偏导。
 */
#pragma once

#include "../math_export.h"

#include "../curve/nurbs_curve.h"
#include "../geom/point.h"
#include "../linalg/vec4.h"

#include <cassert>
#include <utility>
#include <vector>

namespace mulan::math {

class NURBSSurface {
public:
    using ControlGrid = std::vector<std::vector<Point3>>;
    using WeightGrid = std::vector<std::vector<double>>;
    using Row = std::vector<Point3>;
    using KnotVector = std::vector<double>;

    // ---------- 构造 ----------

    /// 控制点 + 次数构造，权重默认 1（退化为 B-spline 曲面），clamped 节点向量。
    NURBSSurface(int p, int q, ControlGrid grid)
        : degree_u_(p),
          degree_v_(q),
          control_points_(std::move(grid)),
          weights_(numRows(), std::vector<double>(numCols(), 1.0)),
          knots_u_(clampedKnotVector(p, numCols())),
          knots_v_(clampedKnotVector(q, numRows())) {
        validateInvariants();
    }

    /// 控制点 + 权重 + clamped 节点向量。
    NURBSSurface(int p, int q, ControlGrid grid, WeightGrid weights)
        : degree_u_(p),
          degree_v_(q),
          control_points_(std::move(grid)),
          weights_(std::move(weights)),
          knots_u_(clampedKnotVector(p, numCols())),
          knots_v_(clampedKnotVector(q, numRows())) {
        validateInvariants();
    }

    /// 完整构造。
    NURBSSurface(int p, int q, ControlGrid grid, WeightGrid weights, KnotVector knotsU, KnotVector knotsV)
        : degree_u_(p),
          degree_v_(q),
          control_points_(std::move(grid)),
          weights_(std::move(weights)),
          knots_u_(std::move(knotsU)),
          knots_v_(std::move(knotsV)) {
        validateInvariants();
    }

    // ---------- 查询 ----------

    int degreeU() const noexcept { return degree_u_; }
    int degreeV() const noexcept { return degree_v_; }
    int numRows() const noexcept { return static_cast<int>(control_points_.size()); }
    int numCols() const noexcept { return static_cast<int>(control_points_[0].size()); }

    const ControlGrid& controlPoints() const noexcept { return control_points_; }
    const WeightGrid& weights() const noexcept { return weights_; }
    const KnotVector& knotsU() const noexcept { return knots_u_; }
    const KnotVector& knotsV() const noexcept { return knots_v_; }

    std::pair<double, double> domainU() const noexcept {
        const int m = static_cast<int>(knots_u_.size()) - 1;
        return { knots_u_[degree_u_], knots_u_[m - degree_u_] };
    }
    std::pair<double, double> domainV() const noexcept {
        const int m = static_cast<int>(knots_v_.size()) - 1;
        return { knots_v_[degree_v_], knots_v_[m - degree_v_] };
    }

    // ---------- 求值 ----------

    /// 齐次张量积 de Boor 求值 + 透视除法。u/v 自动 clamp。
    Point3 evaluate(double u, double v) const;

    // ---------- 偏导与法向 ----------

    /// 有理偏导 (dS/du, dS/dv)。NURBS Book §4.4。
    std::pair<Vec3, Vec3> derivatives(double u, double v) const;

    Vec3 normal(double u, double v) const;

private:
    int degree_u_;
    int degree_v_;
    ControlGrid control_points_;
    WeightGrid weights_;
    KnotVector knots_u_;
    KnotVector knots_v_;

    void validateInvariants() const;

    static double clampToDomain(double t, double lo, double hi) noexcept { return t < lo ? lo : (t > hi ? hi : t); }

    static Point3 perspectiveDivide(const Vec4& h) noexcept { return Point3(h.x / h.w, h.y / h.w, h.z / h.w); }

    static MATH_API std::vector<Vec4> liftRowHomogeneous(const ControlGrid& cp, const WeightGrid& w, int row);

    std::vector<Vec4> liftRowHomogeneous(int row) const;

    /// 在 (u,v) 处求齐次值 H = (A, W)（不透视除法）
    Vec4 evaluateHomogeneous(double u, double v) const;

    /// 去首末各一节点（导数曲线节点向量）
    static MATH_API KnotVector trimmedKnots(const KnotVector& U);

    /// 齐次 hodograph 控制点：Q_i = p·(H_{i+1}-H_i)/(U_{i+p+1}-U_{i+1})。
    static MATH_API std::vector<Vec4> hodographVec4(const std::vector<Vec4>& H, const KnotVector& U, int p);

    /// 把第 col 列提升为齐次 (Vec4) 序列（沿 v 方向）
    std::vector<Vec4> liftColHomogeneous(int col) const;

    /// 通用 de Boor（Vec4 控制点）
    static MATH_API Vec4 deBoorVec4(const std::vector<Vec4>& cp, const KnotVector& U, int p, double u);
};

}  // namespace mulan::math
