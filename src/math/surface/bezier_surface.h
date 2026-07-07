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

#include "../math_export.h"

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
    Point3 evaluate(double u, double v) const;

    /// De Casteljau 双向求值（推荐）。u/v 自动 clamp。
    Point3 deCasteljau(double u, double v) const;

    // ---------- 偏导与法向 ----------

    /// 偏导 (dS/du, dS/dv) 在 (u,v)。两者都是向量（位移）。
    std::pair<Vec3, Vec3> derivatives(double u, double v) const;

    /// 单位法向 = normalize(dS/du × dS/dv)。退化曲面（叉积≈0）回退 UnitZ()。
    Vec3 normal(double u, double v) const;

    // ---------- 结构操作 ----------

    /// u 方向升阶：列数 + 1，曲面几何不变。
    BezierSurface elevateDegreeU() const;

    /// v 方向升阶：行数 + 1，曲面几何不变。
    BezierSurface elevateDegreeV() const;

private:
    ControlGrid control_points_;

    /// 校验网格：非空、每行非空、所有行等长。
    void validate() const;

    static double clampParam(double t) noexcept { return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t); }

    /// 对一条 Bezier 控制点序列在 t 处做 De Casteljau 求值（点仿射组合）。
    static MATH_API Point3 deCasteljauRow(const Row& row, double t);
};

}  // namespace mulan::math
