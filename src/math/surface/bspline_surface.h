/**
 * @file bspline_surface.h
 * @brief B-spline 曲面（张量积）— 仅数学
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学定义（The NURBS Book §3.x / §4.4）：
 *   S(u,v) = Σ_{i=0..n} Σ_{j=0..m} N_{i,p}(u) · N_{j,q}(v) · P_{ij}
 *   P_{ij} 控制点网格 control_points_[j][i]（row = v 方向，col = u 方向）。
 *   p = degreeU（u 方向次数），q = degreeV（v 方向次数）。
 *   U 节点向量长度 = (n+1) + p + 1，V 节点向量长度 = (m+1) + q + 1。
 *   定义域：u ∈ [U[p], U[n+1]]，v ∈ [V[q], V[m+1]]。
 *
 * 与 BezierSurface 的区别：
 *   - Bezier 是 B-spline 的特例（节点向量两端各 p+1 个重复，无内部节点）。
 *   - B-spline 局部支撑 + 内部节点可控 C^k 连续性。
 *
 * 类型设计（同 BezierSurface）：
 *   - 控制点 Point3，导数/法向返回 Vec3。
 *   - 控制点网格外层 v 方向、内层 u 方向。
 *
 * 范围（仅数学）：
 *   只产出曲面上的点 / 偏导 / 法向。tessellation 归调用方。
 *
 * 参数域与边界：
 *   - u/v 自动 clamp 到各自定义域。
 *   - 构造时 assert 校验：网格非空矩形、节点向量长度匹配。
 *
 * 算法来源：
 *   求值：u 方向对每行 de Boor → 得 v 方向控制曲线 → 再 de Boor（NURBS Book A3.5 思路）。
 *   偏导：用 BSplineCurve 的解析导数，分别固定一个方向求另一个方向的导数曲线。
 */
#pragma once

#include "../math_export.h"

#include "../basis/bspline_basis.h"
#include "../curve/bspline_curve.h"
#include "../geom/point.h"
#include "../linalg/vec3.h"

#include <cassert>
#include <utility>
#include <vector>

namespace mulan::math {

class BSplineSurface {
public:
    using ControlGrid = std::vector<std::vector<Point3>>;
    using Row = std::vector<Point3>;
    using KnotVector = std::vector<double>;

    // ---------- 构造 ----------

    /// 控制点 + 次数构造，u/v 方向各自生成 clamped 节点向量。
    /// 前置条件：网格非空矩形；列数 > p，行数 > q。
    BSplineSurface(int p, int q, ControlGrid grid)
        : degree_u_(p),
          degree_v_(q),
          control_points_(std::move(grid)),
          knots_u_(clampedKnotVector(p, numCols())),
          knots_v_(clampedKnotVector(q, numRows())) {
        validateInvariants();
    }

    /// 完整构造（控制点 + 显式 u/v 节点向量）。
    BSplineSurface(int p, int q, ControlGrid grid, KnotVector knotsU, KnotVector knotsV)
        : degree_u_(p),
          degree_v_(q),
          control_points_(std::move(grid)),
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
    const KnotVector& knotsU() const noexcept { return knots_u_; }
    const KnotVector& knotsV() const noexcept { return knots_v_; }

    /// u 方向定义域 [U[p], U[end-p]]
    std::pair<double, double> domainU() const noexcept {
        const int m = static_cast<int>(knots_u_.size()) - 1;
        return { knots_u_[degree_u_], knots_u_[m - degree_u_] };
    }
    /// v 方向定义域
    std::pair<double, double> domainV() const noexcept {
        const int m = static_cast<int>(knots_v_.size()) - 1;
        return { knots_v_[degree_v_], knots_v_[m - degree_v_] };
    }

    Point3 controlPoint(int row, int col) const { return control_points_[row][col]; }
    void setControlPoint(int row, int col, const Point3& p) { control_points_[row][col] = p; }

    // ---------- 求值 ----------

    /// 张量积 de Boor 求值：u 方向对每行求值得一条 v 方向控制曲线，再在 v 方向求值。
    /// u/v 自动 clamp 到定义域。
    Point3 evaluate(double u, double v) const;

    // ---------- 偏导与法向 ----------

    /// 偏导 (dS/du, dS/dv)。u/v 自动 clamp。
    std::pair<Vec3, Vec3> derivatives(double u, double v) const;

    /// 单位法向 = normalize(dS/du × dS/dv)。退化曲面回退 UnitZ()。
    Vec3 normal(double u, double v) const;

private:
    int degree_u_;
    int degree_v_;
    ControlGrid control_points_;
    KnotVector knots_u_;
    KnotVector knots_v_;

    void validateInvariants() const;

    static double clampToDomain(double t, double lo, double hi) noexcept { return t < lo ? lo : (t > hi ? hi : t); }

    /// 对一条 Point3 控制序列做 de Boor 求值
    static MATH_API Point3 deBoorRow(const Row& cp, const KnotVector& U, int p, double u);

    /// 对一条 Vec3 控制序列做 de Boor 求值（偏导结果组合用）
    static MATH_API Vec3 deBoorVecRow(const std::vector<Vec3>& cp, const KnotVector& U, int p, double u);
};

}  // namespace mulan::math
