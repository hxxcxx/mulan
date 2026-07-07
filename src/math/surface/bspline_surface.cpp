#include "bspline_surface.h"

#include "../scalar/tolerance.h"

#include <utility>

namespace mulan::math {

// ---------- 求值 ----------

Point3 BSplineSurface::evaluate(double u, double v) const {
    const auto [umin, umax] = domainU();
    const auto [vmin, vmax] = domainV();
    u = clampToDomain(u, umin, umax);
    v = clampToDomain(v, vmin, vmax);

    // 1) 对每行（u 方向 B-spline 曲线）在 u 处求值，得到 v 方向的控制点序列
    Row col(numRows());
    for (int j = 0; j < numRows(); ++j) {
        col[j] = deBoorRow(control_points_[j], knots_u_, degree_u_, u);
    }
    // 2) 把结果当成 v 方向 B-spline 曲线在 v 处求值
    return deBoorRow(col, knots_v_, degree_v_, v);
}

// ---------- 偏导与法向 ----------

std::pair<Vec3, Vec3> BSplineSurface::derivatives(double u, double v) const {
    const auto [umin, umax] = domainU();
    const auto [vmin, vmax] = domainV();
    u = clampToDomain(u, umin, umax);
    v = clampToDomain(v, vmin, vmax);

    // ---- dS/du ----
    std::vector<Vec3> duCtrl(numRows());
    for (int j = 0; j < numRows(); ++j) {
        BSplineCurve3d rowCurve(degree_u_, control_points_[j], knots_u_);
        duCtrl[j] = rowCurve.derivative(u, 1);
    }
    Vec3 dSdu = deBoorVecRow(duCtrl, knots_v_, degree_v_, v);

    // ---- dS/dv：把列抽出来构造 v 方向曲线 ----
    std::vector<Vec3> dvCtrl(numCols());
    for (int i = 0; i < numCols(); ++i) {
        Row col(numRows());
        for (int j = 0; j < numRows(); ++j)
            col[j] = control_points_[j][i];
        BSplineCurve3d colCurve(degree_v_, col, knots_v_);
        dvCtrl[i] = colCurve.derivative(v, 1);
    }
    Vec3 dSdv = deBoorVecRow(dvCtrl, knots_u_, degree_u_, u);

    return { dSdu, dSdv };
}

Vec3 BSplineSurface::normal(double u, double v) const {
    auto [dSdu, dSdv] = derivatives(u, v);
    Vec3 n = dSdu.cross(dSdv);
    return n.lengthSq() > 1e-24 ? n.normalized() : Vec3::unitZ();
}

// ---------- 私有辅助 ----------

void BSplineSurface::validateInvariants() const {
    assert(!control_points_.empty() && "BSplineSurface: control grid must not be empty");
    const int cols = static_cast<int>(control_points_[0].size());
    assert(cols > 0 && "BSplineSurface: control rows must not be empty");
    for (const auto& row : control_points_) {
        assert(static_cast<int>(row.size()) == cols && "BSplineSurface: all rows must have equal length");
    }
    assert(numCols() > degree_u_ && "BSplineSurface: numCols must be > degreeU");
    assert(numRows() > degree_v_ && "BSplineSurface: numRows must be > degreeV");
    assert(static_cast<int>(knots_u_.size()) == numCols() + degree_u_ + 1 && "BSplineSurface: U knot count mismatch");
    assert(static_cast<int>(knots_v_.size()) == numRows() + degree_v_ + 1 && "BSplineSurface: V knot count mismatch");
}

Point3 BSplineSurface::deBoorRow(const Row& cp, const KnotVector& U, int p, double u) {
    const int n = static_cast<int>(cp.size()) - 1;
    const int k = bsplineFindSpan(n, p, u, U);
    Row d(p + 1);
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
            d[j] = mulan::math::lerp(d[j - 1], d[j], a);
        }
    }
    return d[p];
}

Vec3 BSplineSurface::deBoorVecRow(const std::vector<Vec3>& cp, const KnotVector& U, int p, double u) {
    const int n = static_cast<int>(cp.size()) - 1;
    const int k = bsplineFindSpan(n, p, u, U);
    std::vector<Vec3> d(p + 1);
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

}  // namespace mulan::math
