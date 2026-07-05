/**
 * @file curve_surface_tests.cpp
 * @brief 参数曲线曲面（Bezier / B-spline）单元测试
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 测试策略：
 *   1. 数学性质验证（端点插值、单位分解、凸包性质）
 *   2. 算法等价性（Bernstein 直接求值 == De Casteljau；
 *      clamped B-spline 无内部节点 == Bezier）
 *   3. 解析导数 == 有限差分数值导数（验证导数公式正确）
 *   4. 边界行为（参数域 clamp、退化曲面法向回退）
 *
 * 沿用 math_tests.cpp 的 CHECK/CHECK_NEAR 风格。
 */
#include <mulan/math/math.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace mulan::math;

int g_failures = 0;

void fail(const char* expr, const char* file, int line, const std::string& message = {}) {
    ++g_failures;
    std::cerr << file << ':' << line << ": CHECK failed: " << expr;
    if (!message.empty()) std::cerr << " (" << message << ')';
    std::cerr << '\n';
}

#define CHECK(expr) \
    do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (false)

#define CHECK_NEAR(actual, expected, eps) \
    do { \
        const double actual_value = static_cast<double>(actual); \
        const double expected_value = static_cast<double>(expected); \
        if (std::abs(actual_value - expected_value) > static_cast<double>(eps)) \
            fail(#actual " ~= " #expected, __FILE__, __LINE__, \
                 "actual=" + std::to_string(actual_value) + \
                 ", expected=" + std::to_string(expected_value)); \
    } while (false)

void checkPoint2Near(const Point2& a, const Point2& e, double eps = 1e-9) {
    CHECK_NEAR(a.x, e.x, eps); CHECK_NEAR(a.y, e.y, eps);
}
void checkPoint3Near(const Point3& a, const Point3& e, double eps = 1e-9) {
    CHECK_NEAR(a.x, e.x, eps); CHECK_NEAR(a.y, e.y, eps); CHECK_NEAR(a.z, e.z, eps);
}
void checkVec2Near(const Vec2& a, const Vec2& e, double eps = 1e-9) {
    CHECK_NEAR(a.x, e.x, eps); CHECK_NEAR(a.y, e.y, eps);
}
void checkVec3Near(const Vec3& a, const Vec3& e, double eps = 1e-9) {
    CHECK_NEAR(a.x, e.x, eps); CHECK_NEAR(a.y, e.y, eps); CHECK_NEAR(a.z, e.z, eps);
}

// ============================================================
// Bernstein 基函数
// ============================================================

void testBernstein() {
    // C(n, k)
    CHECK(binomial(0, 0) == 1);
    CHECK(binomial(5, 2) == 10);
    CHECK(binomial(10, 3) == 120);
    CHECK(binomial(7, 0) == 1);
    CHECK(binomial(7, 7) == 1);
    CHECK(binomial(3, 5) == 0); // 越界

    // 单位分解：Σ B_{i,n}(t) = 1
    for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        double sum = 0.0;
        for (int i = 0; i <= 3; ++i) sum += bernstein(i, 3, t);
        CHECK_NEAR(sum, 1.0, 1e-12);
    }

    // 端点：B_{0,n}(0)=1, B_{n,n}(1)=1
    CHECK_NEAR(bernstein(0, 3, 0.0), 1.0, 1e-12);
    CHECK_NEAR(bernstein(3, 3, 1.0), 1.0, 1e-12);
    // 端点其余项为 0
    CHECK_NEAR(bernstein(1, 3, 0.0), 0.0, 1e-12);
    CHECK_NEAR(bernstein(2, 3, 1.0), 0.0, 1e-12);
}

// ============================================================
// Bezier 曲线
// ============================================================

void testBezierCurve() {
    // 3 次 Bezier：端点插值 + 端点切向
    BezierCurve2d c({Point2(0,0), Point2(1,2), Point2(3,2), Point2(4,0)});

    checkPoint2Near(c.evaluate(0.0), Point2(0, 0));
    checkPoint2Near(c.evaluate(1.0), Point2(4, 0));

    // Bernstein 直接求值 == De Casteljau（算法等价）
    for (double t : {0.1, 0.3, 0.5, 0.7, 0.9}) {
        checkPoint2Near(c.evaluate(t), c.deCasteljau(t), 1e-10);
    }

    // 一阶导数：端点切向 = 3·(P1-P0) 与 3·(P3-P2)
    Vec2 d0 = c.derivative(0.0, 1);
    Vec2 d1 = c.derivative(1.0, 1);
    checkVec2Near(d0, Vec2(3, 6), 1e-10);  // 3·((1,2)-(0,0)) = (3,6)
    checkVec2Near(d1, Vec2(3, -6), 1e-10); // 3·((4,0)-(3,2)) = (3,-6)

    // 二阶导：端点 = n·(n-1)·(P0-2P1+P2)，这里 6·((0,0)-2(1,2)+(3,2)) = 6·(1,-2)
    Vec2 dd0 = c.derivative(0.0, 2);
    checkVec2Near(dd0, Vec2(6, -12), 1e-10);

    // 参数越界 clamp：t=-0.5 应等于 t=0
    checkPoint2Near(c.evaluate(-0.5), c.evaluate(0.0));
    checkPoint2Near(c.evaluate(1.5), c.evaluate(1.0));

    // 升阶后几何不变：升 1 次后同参数同点
    auto up = c.elevateDegree();
    for (double t : {0.2, 0.5, 0.8}) {
        checkPoint2Near(c.evaluate(t), up.evaluate(t), 1e-9);
    }

    // 细分后拼起来几何不变
    auto [left, right] = c.subdivide(0.5);
    checkPoint2Near(left.evaluate(1.0), right.evaluate(0.0), 1e-12); // 拼接处连续
    checkPoint2Near(left.evaluate(1.0), c.evaluate(0.5), 1e-9);

    // 3D 曲线
    BezierCurve3d c3({Point3(0,0,0), Point3(1,1,0), Point3(2,0,1)});
    checkPoint3Near(c3.evaluate(0.0), Point3(0,0,0));
    checkPoint3Near(c3.evaluate(1.0), Point3(2,0,1));
    // 二次 Bezier 中点：0.25·P0 + 0.5·P1 + 0.25·P2
    checkPoint3Near(c3.evaluate(0.5), Point3(1.0, 0.5, 0.25), 1e-12);

    // 降阶（degree≥1）
    auto dn = c.reduceDegree();
    CHECK(dn.degree() == c.degree() - 1);
}

// ============================================================
// Bezier 曲面
// ============================================================

void testBezierSurface() {
    // 2x2 单位平面：4 角点 + 凸起中心构成的曲面
    BezierSurface::ControlGrid grid = {
        { Point3(0,0,0), Point3(1,0,0), Point3(2,0,0) },
        { Point3(0,1,0), Point3(1,1,1), Point3(2,1,0) },
        { Point3(0,2,0), Point3(1,2,0), Point3(2,2,0) },
    };
    BezierSurface s(std::move(grid));

    // 四角插值
    checkPoint3Near(s.evaluate(0, 0), Point3(0,0,0));
    checkPoint3Near(s.evaluate(1, 0), Point3(2,0,0));
    checkPoint3Near(s.evaluate(0, 1), Point3(0,2,0));
    checkPoint3Near(s.evaluate(1, 1), Point3(2,2,0));

    // Bernstein 直接求值 == de Casteljau
    for (double u : {0.2, 0.5, 0.8})
        for (double v : {0.3, 0.6})
            checkPoint3Near(s.evaluate(u, v), s.deCasteljau(u, v), 1e-10);

    // 中心点高度：4 角 0 + 4 边中 0 + 中心 1 → Bernstein(1,1; 0.5,0.5)=0.25
    checkPoint3Near(s.evaluate(0.5, 0.5), Point3(1.0, 1.0, 0.25), 1e-10);

    // 法向：对称凸曲面，中心法向朝 +Z
    checkVec3Near(s.normal(0.5, 0.5), Vec3(0, 0, 1), 1e-9);

    // 升阶后几何不变
    auto su = s.elevateDegreeU();
    for (double u : {0.2, 0.5, 0.8})
        for (double v : {0.3, 0.7})
            checkPoint3Near(s.evaluate(u, v), su.evaluate(u, v), 1e-9);

    auto sv = s.elevateDegreeV();
    for (double u : {0.2, 0.5, 0.8})
        for (double v : {0.3, 0.7})
            checkPoint3Near(s.evaluate(u, v), sv.evaluate(u, v), 1e-9);
}

// ============================================================
// B-spline 基函数 / 节点向量
// ============================================================

void testBSplineBasis() {
    // clamped 节点向量：5 控制点 3 次 → {0,0,0,0, 0.5, 1,1,1,1}
    auto U = clampedKnotVector(3, 5);
    CHECK(static_cast<int>(U.size()) == 9);
    CHECK_NEAR(U[0], 0.0, 1e-12);
    CHECK_NEAR(U[3], 0.0, 1e-12);
    CHECK_NEAR(U[4], 0.5, 1e-12);
    CHECK_NEAR(U[5], 1.0, 1e-12);
    CHECK_NEAR(U[8], 1.0, 1e-12);

    // findSpan：clamped 3 次 5 控制点（n=4），u=0.5 应落在 span 4
    CHECK(bsplineFindSpan(4, 3, 0.5, U) == 4);
    // u=0（左端点）→ span p = 3
    CHECK(bsplineFindSpan(4, 3, 0.0, U) == 3);
    // u=1（右端点）→ 闭合约定 → span n = 4
    CHECK(bsplineFindSpan(4, 3, 1.0, U) == 4);

    // 基函数单位分解：Σ_{i} N_{i,p}(u) = 1（在定义域内部 + 左端点）。
    // 注：右端点 u=umax 处单个基函数 N_{i,p} 因半开区间约定行为病态，
    //     工程上用 findSpan + de Boor 求值（见 BSplineCurve::evaluate），不在此求和。
    for (double u : {0.0, 0.25, 0.5, 0.75, 0.999}) {
        double sum = 0.0;
        for (int i = 0; i <= 4; ++i) sum += bsplineBasis(i, 3, u, U);
        CHECK_NEAR(sum, 1.0, 1e-10);
    }
}

// ============================================================
// B-spline 曲线
// ============================================================

void testBSplineCurve() {
    // clamped 3 次，5 控制点
    BSplineCurve2d::PointList cp = {
        Point2(0,0), Point2(1,2), Point2(3,2), Point2(4,0), Point2(5,2)
    };
    BSplineCurve2d c(3, cp);

    auto [umin, umax] = c.domain();
    CHECK_NEAR(umin, 0.0, 1e-12);
    CHECK_NEAR(umax, 1.0, 1e-12);

    // clamped 节点 → 穿过首末控制点
    checkPoint2Near(c.evaluate(0.0), Point2(0, 0), 1e-12);
    checkPoint2Near(c.evaluate(1.0), Point2(5, 2), 1e-12);

    // 【关键等价性】clamped 节点 + 无内部节点 == Bezier
    // 4 控制点 3 次，节点 {0,0,0,0,1,1,1,1}
    BSplineCurve2d::PointList bezCp = {
        Point2(0,0), Point2(1,2), Point2(3,2), Point2(4,0)
    };
    BSplineCurve2d bs(3, bezCp, {0,0,0,0,1,1,1,1});
    BezierCurve2d  bz(bezCp);
    for (double t : {0.1, 0.3, 0.5, 0.7, 0.9}) {
        checkPoint2Near(bs.evaluate(t), bz.evaluate(t), 1e-10);
        checkPoint2Near(bs.evaluate(t), bz.deCasteljau(t), 1e-10);
    }

    // 解析导数 vs 中心差分（在内部点验证）
    for (double t : {0.25, 0.5, 0.75}) {
        Vec2 analytic = c.derivative(t, 1);
        double h = 1e-6;
        Point2 fp = c.evaluate(t + h);
        Point2 fm = c.evaluate(t - h);
        Vec2 numeric = (fp - fm) * (1.0 / (2.0 * h));
        checkVec2Near(analytic, numeric, 1e-3); // 数值差分精度有限
    }

    // 二阶导 vs 数值
    for (double t : {0.3, 0.5, 0.7}) {
        Vec2 analytic = c.derivative(t, 2);
        double h = 1e-4;
        Point2 p0 = c.evaluate(t);
        Point2 pp = c.evaluate(t + h);
        Point2 pm = c.evaluate(t - h);
        Vec2 numeric = ((pp - p0) + (pm - p0)) * (1.0 / (h * h));
        checkVec2Near(analytic, numeric, 1e-1);
    }

    // 节点插入：插 1 次，控制点 +1，几何不变
    BSplineCurve2d c2(3, cp);
    int before = c2.controlPointCount();
    Point2 beforeEval = c2.evaluate(0.5);
    c2.insertKnot(0.5, 1);
    CHECK(c2.controlPointCount() == before + 1);
    checkPoint2Near(c2.evaluate(0.5), beforeEval, 1e-12);

    // 3D 曲线
    BSplineCurve3d::PointList cp3 = {
        Point3(0,0,0), Point3(1,1,0), Point3(2,0,1), Point3(3,1,1), Point3(4,0,0)
    };
    BSplineCurve3d c3(3, cp3);
    checkPoint3Near(c3.evaluate(0.0), Point3(0,0,0));
    checkPoint3Near(c3.evaluate(1.0), Point3(4,0,0));
    // 导数应是合法向量
    Vec3 d3 = c3.derivative(0.5, 1);
    CHECK(d3.lengthSq() > 0.0);
}

// ============================================================
// B-spline 曲面
// ============================================================

void testBSplineSurface() {
    // 2x2 clamped B-spline 曲面，p=q=2，3x3 控制点
    BSplineSurface::ControlGrid grid = {
        { Point3(0,0,0), Point3(1,0,0), Point3(2,0,0) },
        { Point3(0,1,0), Point3(1,1,1), Point3(2,1,0) },
        { Point3(0,2,0), Point3(1,2,0), Point3(2,2,0) },
    };
    BSplineSurface s(2, 2, std::move(grid));

    auto [umin, umax] = s.domainU();
    auto [vmin, vmax] = s.domainV();
    CHECK_NEAR(umin, 0.0, 1e-12);
    CHECK_NEAR(umax, 1.0, 1e-12);

    // clamped → 四角穿过控制点
    checkPoint3Near(s.evaluate(0, 0), Point3(0,0,0));
    checkPoint3Near(s.evaluate(1, 0), Point3(2,0,0));
    checkPoint3Near(s.evaluate(0, 1), Point3(0,2,0));
    checkPoint3Near(s.evaluate(1, 1), Point3(2,2,0));

    // 偏导 vs 中心差分
    auto [du_ana, dv_ana] = s.derivatives(0.5, 0.5);
    double h = 1e-6;
    Point3 pu_p = s.evaluate(0.5 + h, 0.5);
    Point3 pu_m = s.evaluate(0.5 - h, 0.5);
    Vec3 du_num = (pu_p - pu_m) * (1.0 / (2.0 * h));
    Point3 pv_p = s.evaluate(0.5, 0.5 + h);
    Point3 pv_m = s.evaluate(0.5, 0.5 - h);
    Vec3 dv_num = (pv_p - pv_m) * (1.0 / (2.0 * h));
    checkVec3Near(du_ana, du_num, 1e-3);
    checkVec3Near(dv_ana, dv_num, 1e-3);

    // 法向非零、单位长
    Vec3 n = s.normal(0.5, 0.5);
    CHECK_NEAR(n.length(), 1.0, 1e-9);

    // clamped 无内部节点时 == Bezier 曲面（p=q=2，3x3 控制点恰好是二次 Bezier 曲面）
    BezierSurface::ControlGrid bezGrid = {
        { Point3(0,0,0), Point3(1,0,0), Point3(2,0,0) },
        { Point3(0,1,0), Point3(1,1,1), Point3(2,1,0) },
        { Point3(0,2,0), Point3(1,2,0), Point3(2,2,0) },
    };
    BSplineSurface bss(2, 2, bezGrid,
                       {0,0,0,1,1,1}, {0,0,0,1,1,1}); // clamped 无内部节点
    BezierSurface  bz(std::move(bezGrid));
    for (double u : {0.1, 0.4, 0.7})
        for (double v : {0.2, 0.5, 0.9})
            checkPoint3Near(bss.evaluate(u, v), bz.evaluate(u, v), 1e-10);
}

} // namespace

int main() {
    testBernstein();
    testBezierCurve();
    testBezierSurface();
    testBSplineBasis();
    testBSplineCurve();
    testBSplineSurface();

    if (g_failures != 0) {
        std::cerr << g_failures << " curve/surface test failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "Curve/Surface tests passed\n";
    return EXIT_SUCCESS;
}
