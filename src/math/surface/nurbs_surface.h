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
    Point3 evaluate(double u, double v) const {
        const auto [umin, umax] = domainU();
        const auto [vmin, vmax] = domainV();
        u = clampToDomain(u, umin, umax);
        v = clampToDomain(v, vmin, vmax);

        // 1) 每行提升为齐次 (Vec4) → u 方向 de Boor → 得到 v 方向齐次控制曲线
        std::vector<Vec4> vCtrl(numRows());
        for (int j = 0; j < numRows(); ++j) {
            std::vector<Vec4> rowHomog = liftRowHomogeneous(j);
            vCtrl[j] = deBoorVec4(rowHomog, knots_u_, degree_u_, u);
        }
        // 2) v 方向 de Boor + 透视除法
        Vec4 h = deBoorVec4(vCtrl, knots_v_, degree_v_, v);
        return perspectiveDivide(h);
    }

    // ---------- 偏导与法向 ----------

    /// 有理偏导 (dS/du, dS/dv)。NURBS Book §4.4：
    ///   S_u = (A_u - S·W_u) / W
    /// 其中 A 是齐次前 3 分量、W 是末分量；A_u/W_u 用齐次 hodograph 求。
    /// 实现：对每个方向，先在齐次空间求该方向 hodograph 曲线，在 u/v 处求值得
    /// 中间齐次控制点序列，再用另一方向的齐次 B-spline 组合。
    std::pair<Vec3, Vec3> derivatives(double u, double v) const {
        const auto [umin, umax] = domainU();
        const auto [vmin, vmax] = domainV();
        u = clampToDomain(u, umin, umax);
        v = clampToDomain(v, vmin, vmax);
        const double eps = Tolerance::defaultValue().paramEps;

        Vec4 h = evaluateHomogeneous(u, v);
        double w = h.w;
        assert(std::abs(w) > eps && "NURBSSurface::derivatives: degenerate weight");
        Point3 s = perspectiveDivide(h);

        // ---- dS/du ----
        // 每行：u 方向齐次 hodograph 控制点 Q^u_j[i] = p·(H_{j,i+1}-H_{j,i})/(U_{i+p+1}-U_{i+1})
        //       构成 (p-1) 次齐次 B-spline 曲线（节点 = U 去首末各一）。
        //   每行在 u 处求值得 dU[j]（Vec4），作为 v 方向齐次控制点；v 方向 p 次组合。
        const KnotVector uKnots = trimmedKnots(knots_u_);
        std::vector<Vec4> dU(numRows());
        for (int j = 0; j < numRows(); ++j) {
            std::vector<Vec4> rowHomog = liftRowHomogeneous(j);
            std::vector<Vec4> Q = hodographVec4(rowHomog, knots_u_, degree_u_);
            dU[j] = deBoorVec4(Q, uKnots, degree_u_ - 1, u);
        }
        Vec4 hu = deBoorVec4(dU, knots_v_, degree_v_, v);
        Vec3 dSdu = (Vec3(hu.x, hu.y, hu.z) - s.asVec() * hu.w) * (1.0 / w);

        // ---- dS/dv（对称）----
        const KnotVector vKnots = trimmedKnots(knots_v_);
        std::vector<Vec4> dV(numCols());
        for (int i = 0; i < numCols(); ++i) {
            std::vector<Vec4> colHomog = liftColHomogeneous(i);
            std::vector<Vec4> Q = hodographVec4(colHomog, knots_v_, degree_v_);
            dV[i] = deBoorVec4(Q, vKnots, degree_v_ - 1, v);
        }
        Vec4 hv = deBoorVec4(dV, knots_u_, degree_u_, u);
        Vec3 dSdv = (Vec3(hv.x, hv.y, hv.z) - s.asVec() * hv.w) * (1.0 / w);

        return { dSdu, dSdv };
    }

    Vec3 normal(double u, double v) const {
        auto [dSdu, dSdv] = derivatives(u, v);
        Vec3 n = dSdu.cross(dSdv);
        return n.lengthSq() > 1e-24 ? n.normalized() : Vec3::unitZ();
    }

private:
    int degree_u_;
    int degree_v_;
    ControlGrid control_points_;
    WeightGrid weights_;
    KnotVector knots_u_;
    KnotVector knots_v_;

    void validateInvariants() const {
        assert(!control_points_.empty() && "NURBSSurface: control grid must not be empty");
        const int cols = static_cast<int>(control_points_[0].size());
        assert(cols > 0 && "NURBSSurface: control rows must not be empty");
        for (const auto& row : control_points_) {
            assert(static_cast<int>(row.size()) == cols && "NURBSSurface: all rows must have equal length");
        }
        assert(static_cast<int>(weights_.size()) == numRows() && "NURBSSurface: weights row count mismatch");
        for (const auto& row : weights_) {
            assert(static_cast<int>(row.size()) == cols && "NURBSSurface: weights row length mismatch");
        }
        assert(numCols() > degree_u_ && "NURBSSurface: numCols must be > degreeU");
        assert(numRows() > degree_v_ && "NURBSSurface: numRows must be > degreeV");
        assert(static_cast<int>(knots_u_.size()) == numCols() + degree_u_ + 1);
        assert(static_cast<int>(knots_v_.size()) == numRows() + degree_v_ + 1);
    }

    static double clampToDomain(double t, double lo, double hi) noexcept { return t < lo ? lo : (t > hi ? hi : t); }

    static Point3 perspectiveDivide(const Vec4& h) noexcept { return Point3(h.x / h.w, h.y / h.w, h.z / h.w); }

    static std::vector<Vec4> liftRowHomogeneous(const ControlGrid& cp, const WeightGrid& w, int row) {
        std::vector<Vec4> h(cp[row].size());
        for (size_t i = 0; i < cp[row].size(); ++i) {
            double wi = w[row][i];
            const Point3& p = cp[row][i];
            h[i] = Vec4(p.x * wi, p.y * wi, p.z * wi, wi);
        }
        return h;
    }

    std::vector<Vec4> liftRowHomogeneous(int row) const { return liftRowHomogeneous(control_points_, weights_, row); }

    /// 在 (u,v) 处求齐次值 H = (A, W)（不透视除法）
    Vec4 evaluateHomogeneous(double u, double v) const {
        std::vector<Vec4> vCtrl(numRows());
        for (int j = 0; j < numRows(); ++j) {
            vCtrl[j] = deBoorVec4(liftRowHomogeneous(j), knots_u_, degree_u_, u);
        }
        return deBoorVec4(vCtrl, knots_v_, degree_v_, v);
    }

    // 去首末各一节点（导数曲线节点向量）
    static KnotVector trimmedKnots(const KnotVector& U) { return KnotVector(U.begin() + 1, U.end() - 1); }

    /// 齐次 hodograph 控制点：Q_i = p·(H_{i+1}-H_i)/(U_{i+p+1}-U_{i+1})。
    /// 对任意一维齐次控制序列适用（行/列）。
    static std::vector<Vec4> hodographVec4(const std::vector<Vec4>& H, const KnotVector& U, int p) {
        std::vector<Vec4> Q;
        Q.reserve(H.size() - 1);
        for (size_t i = 0; i + 1 < H.size(); ++i) {
            double denom = U[i + p + 1] - U[i + 1];
            double scale = std::abs(denom) > Tolerance::defaultValue().paramEps ? static_cast<double>(p) / denom : 0.0;
            Q.push_back((H[i + 1] - H[i]) * scale);
        }
        return Q;
    }

    /// 把第 col 列提升为齐次 (Vec4) 序列（沿 v 方向）
    std::vector<Vec4> liftColHomogeneous(int col) const {
        std::vector<Vec4> h(numRows());
        for (int j = 0; j < numRows(); ++j) {
            double wj = weights_[j][col];
            const Point3& p = control_points_[j][col];
            h[j] = Vec4(p.x * wj, p.y * wj, p.z * wj, wj);
        }
        return h;
    }

    // 通用 de Boor（Vec4 控制点）
    static Vec4 deBoorVec4(const std::vector<Vec4>& cp, const KnotVector& U, int p, double u) {
        const int n = static_cast<int>(cp.size()) - 1;
        const int k = bsplineFindSpan(n, p, u, U);
        std::vector<Vec4> d(p + 1);
        for (int j = 0; j <= p; ++j)
            d[j] = cp[k - p + j];
        for (int r = 1; r <= p; ++r) {
            for (int j = p; j >= r; --j) {
                const int idx = k - p + j;
                double denom = U[idx + p + 1 - r] - U[idx];
                double a = 0.0;
                if (std::abs(denom) > Tolerance::defaultValue().paramEps) {
                    a = (u - U[idx]) / denom;
                }
                d[j] = d[j - 1] + (d[j] - d[j - 1]) * a;
            }
        }
        return d[p];
    }
};

}  // namespace mulan::math
