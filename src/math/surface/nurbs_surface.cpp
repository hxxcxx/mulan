#include "nurbs_surface.h"

#include <utility>

namespace mulan::math {

// ---------- 求值 ----------

Point3 NURBSSurface::evaluate(double u, double v) const {
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

std::pair<Vec3, Vec3> NURBSSurface::derivatives(double u, double v) const {
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

Vec3 NURBSSurface::normal(double u, double v) const {
    auto [dSdu, dSdv] = derivatives(u, v);
    Vec3 n = dSdu.cross(dSdv);
    return n.lengthSq() > 1e-24 ? n.normalized() : Vec3::unitZ();
}

// ---------- 私有辅助 ----------

void NURBSSurface::validateInvariants() const {
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

std::vector<Vec4> NURBSSurface::liftRowHomogeneous(const ControlGrid& cp, const WeightGrid& w, int row) {
    std::vector<Vec4> h(cp[row].size());
    for (size_t i = 0; i < cp[row].size(); ++i) {
        double wi = w[row][i];
        const Point3& p = cp[row][i];
        h[i] = Vec4(p.x * wi, p.y * wi, p.z * wi, wi);
    }
    return h;
}

std::vector<Vec4> NURBSSurface::liftRowHomogeneous(int row) const {
    return liftRowHomogeneous(control_points_, weights_, row);
}

Vec4 NURBSSurface::evaluateHomogeneous(double u, double v) const {
    std::vector<Vec4> vCtrl(numRows());
    for (int j = 0; j < numRows(); ++j) {
        vCtrl[j] = deBoorVec4(liftRowHomogeneous(j), knots_u_, degree_u_, u);
    }
    return deBoorVec4(vCtrl, knots_v_, degree_v_, v);
}

NURBSSurface::KnotVector NURBSSurface::trimmedKnots(const KnotVector& U) {
    return KnotVector(U.begin() + 1, U.end() - 1);
}

std::vector<Vec4> NURBSSurface::hodographVec4(const std::vector<Vec4>& H, const KnotVector& U, int p) {
    std::vector<Vec4> Q;
    Q.reserve(H.size() - 1);
    for (size_t i = 0; i + 1 < H.size(); ++i) {
        double denom = U[i + p + 1] - U[i + 1];
        double scale = std::abs(denom) > Tolerance::defaultValue().paramEps ? static_cast<double>(p) / denom : 0.0;
        Q.push_back((H[i + 1] - H[i]) * scale);
    }
    return Q;
}

std::vector<Vec4> NURBSSurface::liftColHomogeneous(int col) const {
    std::vector<Vec4> h(numRows());
    for (int j = 0; j < numRows(); ++j) {
        double wj = weights_[j][col];
        const Point3& p = control_points_[j][col];
        h[j] = Vec4(p.x * wj, p.y * wj, p.z * wj, wj);
    }
    return h;
}

Vec4 NURBSSurface::deBoorVec4(const std::vector<Vec4>& cp, const KnotVector& U, int p, double u) {
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

}  // namespace mulan::math
