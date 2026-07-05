/**
 * @file bezier_surface.h
 * @brief 双三次 Bezier 曲面（张量积）— 仅数学
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学定义：
 *   张量积 Bezier 曲面：
 *     S(u,v) = Σ_{i=0..n} Σ_{j=0..m} B_{i,n}(u) · B_{j,m}(v) · P_{ij}
 *   u,v ∈ [0,1]。P_{ij} 为控制点网格 control_points_[j][i]（row = v 方向，col = u 方向）。
 *   degreeU = 列数 - 1，degreeV = 行数 - 1。
 *
 * 类型设计：
 *   - 控制点为 Point3（位置），导数/法向返回 Vec3（位移）。
 *   - 控制点网格：std::vector<std::vector<Point3>>，外层 = v 方向行，内层 = u 方向列。
 *
 * 范围（仅数学）：
 *   本类只产出曲面上的点 / 偏导 / 法向。tessellation（按分辨率采样成顶点网格）、
 *   控制网可视化、Mesh 生成等归调用方（引擎层）实现 —— 通过 evaluate/deCasteljau/
 *   normal 即可在任意分辨率下采样。
 *
 * 参数域与边界：
 *   - u/v 自动 clamp 到 [0,1]。
 *   - 控制点网格必须为非空矩形（所有行长度一致），构造时 assert 校验。
 *
 * 算法来源：
 *   The NURBS Book §3.1 / Farin —— de Casteljau 双向求值、Hodograph 求偏导。
 */
#pragma once

#include "../basis/bernstein.h"
#include "../geom/point.h"
#include "../linalg/vec3.h"

#include <cassert>
#include <vector>

namespace mulan::math {

class BezierSurface {
public:
    /// 控制点网格：grid[v][u]，外层 v 方向、内层 u 方向
    using ControlGrid = std::vector<std::vector<Point3>>;
    using Row = std::vector<Point3>;

    // ---------- 构造 ----------

    /// 用控制点网格构造。前置条件：非空矩形网格。
    explicit BezierSurface(ControlGrid grid) : control_points_(std::move(grid)) { validate(); }

    // ---------- 维度查询 ----------

    /// u 方向次数 = 列数 - 1
    int degreeU() const noexcept { return static_cast<int>(control_points_[0].size()) - 1; }
    /// v 方向次数 = 行数 - 1
    int degreeV() const noexcept { return static_cast<int>(control_points_.size()) - 1; }
    int numRows() const noexcept { return static_cast<int>(control_points_.size()); }
    int numCols() const noexcept { return static_cast<int>(control_points_[0].size()); }

    const ControlGrid& controlPoints() const noexcept { return control_points_; }

    Point3 controlPoint(int row, int col) const { return control_points_[row][col]; }
    void setControlPoint(int row, int col, const Point3& p) { control_points_[row][col] = p; }

    // ---------- 求值 ----------

    /// 直接用 Bernstein 基求值。u/v 自动 clamp。注：de Casteljau 数值更稳定。
    Point3 evaluate(double u, double v) const {
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

    /// De Casteljau 双向求值（推荐）。u/v 自动 clamp。
    /// 算法：先对每一行（u 方向 Bezier 曲线）在 u 处求值得 m+1 个点，
    ///       再把结果当成 v 方向 Bezier 曲线在 v 处求值。
    Point3 deCasteljau(double u, double v) const {
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

    /// 偏导 (dS/du, dS/dv) 在 (u,v)。两者都是向量（位移）。
    /// 算法：对每个方向求 Hodograph（导数曲线）控制点，再以 Bernstein 求值。
    std::pair<Vec3, Vec3> derivatives(double u, double v) const {
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

    /// 单位法向 = normalize(dS/du × dS/dv)。退化曲面（叉积≈0）回退 UnitZ()。
    Vec3 normal(double u, double v) const {
        auto [dSdu, dSdv] = derivatives(u, v);
        Vec3 n = dSdu.cross(dSdv);
        // 长度近乎 0 时曲面在该点退化（如平面奇异点），回退 +Z 避免返回 NaN
        return n.lengthSq() > 1e-24 ? n.normalized() : Vec3::unitZ();
    }

    // ---------- 结构操作 ----------

    /// u 方向升阶：列数 + 1，曲面几何不变。
    BezierSurface elevateDegreeU() const {
        const int n = degreeU();
        const int m = degreeV();

        ControlGrid grid;
        grid.reserve(m + 1);
        for (int j = 0; j <= m; ++j) {
            Row row;
            row.reserve(n + 2);
            row.push_back(control_points_[j][0]);
            for (int i = 1; i <= n; ++i) {
                // lerp(P_i, P_{i-1}, i/(n+1)) = (1-a)·P_i + a·P_{i-1}
                double a = static_cast<double>(i) / (n + 1);
                row.push_back(lerp(control_points_[j][i], control_points_[j][i - 1], a));
            }
            row.push_back(control_points_[j][n]);
            grid.push_back(std::move(row));
        }
        return BezierSurface(std::move(grid));
    }

    /// v 方向升阶：行数 + 1，曲面几何不变。
    BezierSurface elevateDegreeV() const {
        const int n = degreeU();
        const int m = degreeV();

        ControlGrid grid;
        grid.reserve(m + 2);
        grid.push_back(control_points_[0]);  // j = 0
        for (int j = 1; j <= m; ++j) {
            double a = static_cast<double>(j) / (m + 1);
            Row row;
            row.reserve(n + 1);
            for (int i = 0; i <= n; ++i) {
                row.push_back(lerp(control_points_[j][i], control_points_[j - 1][i], a));
            }
            grid.push_back(std::move(row));
        }
        grid.push_back(control_points_[m]);  // j = m+1
        return BezierSurface(std::move(grid));
    }

private:
    ControlGrid control_points_;

    /// 校验网格：非空、每行非空、所有行等长。
    void validate() const {
        assert(!control_points_.empty() && "BezierSurface: control grid must not be empty");
        const size_t cols = control_points_[0].size();
        assert(cols > 0 && "BezierSurface: control rows must not be empty");
        for (const auto& row : control_points_) {
            assert(row.size() == cols && "BezierSurface: all rows must have equal length");
        }
    }

    static double clampParam(double t) noexcept { return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t); }

    /// 对一条 Bezier 控制点序列在 t 处做 De Casteljau 求值（点仿射组合）。
    static Point3 deCasteljauRow(const Row& row, double t) {
        Row work = row;
        const int n = static_cast<int>(work.size()) - 1;
        for (int k = 1; k <= n; ++k) {
            for (int i = 0; i <= n - k; ++i) {
                work[i] = lerp(work[i], work[i + 1], t);
            }
        }
        return work[0];
    }
};

}  // namespace mulan::math
