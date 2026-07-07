#include "bezier_surface.h"

#include <utility>

namespace mulan::math {

// ---------- 求值 ----------

Point3 BezierSurface::evaluate(double u, double v) const {
    u = clampParam(u);
    v = clampParam(v);
    const int n = degreeU();
    const int m = degreeV();

    // 以 P_{00} 为基准累加位移，避免 Point + Point
    const Point3 origin = control_points_[0][0];
    Vec3 acc = Vec3::zero();
    for (int j = 0; j <= m; ++j) {
        const double bj = bernstein(j, m, v);
        for (int i = 0; i <= n; ++i) {
            const double bi = bernstein(i, n, u);
            acc += (bi * bj) * (control_points_[j][i] - origin);
        }
    }
    return origin + acc;
}

Point3 BezierSurface::deCasteljau(double u, double v) const {
    u = clampParam(u);
    v = clampParam(v);
    const int m = degreeV();

    Row col(numRows());
    for (int j = 0; j <= m; ++j) {
        col[j] = deCasteljauRow(control_points_[j], u);
    }
    return deCasteljauRow(col, v);
}

// ---------- 偏导与法向 ----------

std::pair<Vec3, Vec3> BezierSurface::derivatives(double u, double v) const {
    u = clampParam(u);
    v = clampParam(v);
    const int n = degreeU();
    const int m = degreeV();

    // ---- dS/du：每行做 u 方向差分（n 个向量），Bernstein(u, n-1) 求和，
    //              再对 v 方向用 Bernstein(v, m) 组合 ----
    std::vector<Vec3> duRow(m + 1, Vec3::zero());
    for (int j = 0; j <= m; ++j) {
        for (int i = 0; i < n; ++i) {
            Vec3 edge = control_points_[j][i + 1] - control_points_[j][i];
            duRow[j] += bernstein(i, n - 1, u) * edge;
        }
        duRow[j] = duRow[j] * static_cast<double>(n);
    }
    Vec3 dSdu = Vec3::zero();
    for (int j = 0; j <= m; ++j) {
        dSdu += bernstein(j, m, v) * duRow[j];
    }

    // ---- dS/dv：每列做 v 方向差分（m 个向量），Bernstein(v, m-1) 求和，
    //              再对 u 方向用 Bernstein(u, n) 组合 ----
    std::vector<Vec3> dvCol(n + 1, Vec3::zero());
    for (int i = 0; i <= n; ++i) {
        for (int j = 0; j < m; ++j) {
            Vec3 edge = control_points_[j + 1][i] - control_points_[j][i];
            dvCol[i] += bernstein(j, m - 1, v) * edge;
        }
        dvCol[i] = dvCol[i] * static_cast<double>(m);
    }
    Vec3 dSdv = Vec3::zero();
    for (int i = 0; i <= n; ++i) {
        dSdv += bernstein(i, n, u) * dvCol[i];
    }

    return { dSdu, dSdv };
}

Vec3 BezierSurface::normal(double u, double v) const {
    auto [dSdu, dSdv] = derivatives(u, v);
    Vec3 n = dSdu.cross(dSdv);
    return n.lengthSq() > 1e-24 ? n.normalized() : Vec3::unitZ();
}

// ---------- 结构操作 ----------

BezierSurface BezierSurface::elevateDegreeU() const {
    const int n = degreeU();
    const int m = degreeV();

    ControlGrid grid;
    grid.reserve(m + 1);
    for (int j = 0; j <= m; ++j) {
        Row row;
        row.reserve(n + 2);
        row.push_back(control_points_[j][0]);
        for (int i = 1; i <= n; ++i) {
            double a = static_cast<double>(i) / (n + 1);
            row.push_back(lerp(control_points_[j][i], control_points_[j][i - 1], a));
        }
        row.push_back(control_points_[j][n]);
        grid.push_back(std::move(row));
    }
    return BezierSurface(std::move(grid));
}

BezierSurface BezierSurface::elevateDegreeV() const {
    const int n = degreeU();
    const int m = degreeV();

    ControlGrid grid;
    grid.reserve(m + 2);
    grid.push_back(control_points_[0]);
    for (int j = 1; j <= m; ++j) {
        double a = static_cast<double>(j) / (m + 1);
        Row row;
        row.reserve(n + 1);
        for (int i = 0; i <= n; ++i) {
            row.push_back(lerp(control_points_[j][i], control_points_[j - 1][i], a));
        }
        grid.push_back(std::move(row));
    }
    grid.push_back(control_points_[m]);
    return BezierSurface(std::move(grid));
}

// ---------- 私有辅助 ----------

void BezierSurface::validate() const {
    assert(!control_points_.empty() && "BezierSurface: control grid must not be empty");
    const size_t cols = control_points_[0].size();
    assert(cols > 0 && "BezierSurface: control rows must not be empty");
    for (const auto& row : control_points_) {
        assert(row.size() == cols && "BezierSurface: all rows must have equal length");
    }
}

Point3 BezierSurface::deCasteljauRow(const Row& row, double t) {
    Row work = row;
    const int n = static_cast<int>(work.size()) - 1;
    for (int k = 1; k <= n; ++k) {
        for (int i = 0; i <= n - k; ++i) {
            work[i] = lerp(work[i], work[i + 1], t);
        }
    }
    return work[0];
}

}  // namespace mulan::math
