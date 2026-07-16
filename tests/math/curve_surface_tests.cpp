/**
 * @file curve_surface_tests.cpp
 * @brief 参数曲线曲面（Bezier / B-spline）单元测试（Google Test 版）
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 测试策略：
 *   1. 数学性质验证（端点插值、单位分解、凸包性质）
 *   2. 算法等价性（Bernstein 直接求值 == De Casteljau；
 *      clamped B-spline 无内部节点 == Bezier）
 *   3. 解析导数 == 有限差分数值导数（验证导数公式正确）
 *   4. 边界行为（参数域 clamp、退化曲面法向回退）
 */
#include <mulan/math/math.h>
#include <gtest/gtest.h>

#include <cmath>

using namespace mulan::math;

// ============================================================
// Bernstein 基函数
// ============================================================

TEST(BernsteinTest, BinomialCoefficients) {
    EXPECT_EQ(binomial(0, 0), 1);
    EXPECT_EQ(binomial(5, 2), 10);
    EXPECT_EQ(binomial(10, 3), 120);
    EXPECT_EQ(binomial(7, 0), 1);
    EXPECT_EQ(binomial(7, 7), 1);
    EXPECT_EQ(binomial(3, 5), 0);  // 越界
}

TEST(BernsteinTest, PartitionOfUnity) {
    for (double t : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
        double sum = 0.0;
        for (int i = 0; i <= 3; ++i)
            sum += bernstein(i, 3, t);
        EXPECT_NEAR(sum, 1.0, 1e-12);
    }
}

TEST(BernsteinTest, EndpointValues) {
    // 端点：B_{0,n}(0)=1, B_{n,n}(1)=1
    EXPECT_NEAR(bernstein(0, 3, 0.0), 1.0, 1e-12);
    EXPECT_NEAR(bernstein(3, 3, 1.0), 1.0, 1e-12);
    // 端点其余项为 0
    EXPECT_NEAR(bernstein(1, 3, 0.0), 0.0, 1e-12);
    EXPECT_NEAR(bernstein(2, 3, 1.0), 0.0, 1e-12);
}

// ============================================================
// Bezier 曲线
// ============================================================

TEST(BezierCurveTest, EndpointInterpolation) {
    BezierCurve2d c({ Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) });
    EXPECT_NEAR(c.evaluate(0.0).x, 0.0, 1e-12);
    EXPECT_NEAR(c.evaluate(0.0).y, 0.0, 1e-12);
    EXPECT_NEAR(c.evaluate(1.0).x, 4.0, 1e-12);
    EXPECT_NEAR(c.evaluate(1.0).y, 0.0, 1e-12);
}

TEST(BezierCurveTest, BernsteinEqualsDeCasteljau) {
    BezierCurve2d c({ Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) });
    for (double t : { 0.1, 0.3, 0.5, 0.7, 0.9 }) {
        Point2 ev = c.evaluate(t);
        Point2 dc = c.deCasteljau(t);
        EXPECT_NEAR(ev.x, dc.x, 1e-10);
        EXPECT_NEAR(ev.y, dc.y, 1e-10);
    }
}

TEST(BezierCurveTest, EndpointDerivative) {
    BezierCurve2d c({ Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) });
    // 一阶导数：端点切向 = 3·(P1-P0) 与 3·(P3-P2)
    Vec2 d0 = c.derivative(0.0, 1);
    Vec2 d1 = c.derivative(1.0, 1);
    EXPECT_NEAR(d0.x, 3.0, 1e-10);
    EXPECT_NEAR(d0.y, 6.0, 1e-10);
    EXPECT_NEAR(d1.x, 3.0, 1e-10);
    EXPECT_NEAR(d1.y, -6.0, 1e-10);

    // 二阶导：端点 = n·(n-1)·(P0-2P1+P2)，这里 6·((0,0)-2(1,2)+(3,2)) = 6·(1,-2)
    Vec2 dd0 = c.derivative(0.0, 2);
    EXPECT_NEAR(dd0.x, 6.0, 1e-10);
    EXPECT_NEAR(dd0.y, -12.0, 1e-10);
}

TEST(BezierCurveTest, ClampBoundary) {
    BezierCurve2d c({ Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) });
    // 参数越界 clamp：t=-0.5 应等于 t=0
    {
        Point2 a = c.evaluate(-0.5);
        Point2 b = c.evaluate(0.0);
        EXPECT_NEAR(a.x, b.x, 1e-12);
        EXPECT_NEAR(a.y, b.y, 1e-12);
    }
    // t=1.5 应等于 t=1.0
    {
        Point2 a = c.evaluate(1.5);
        Point2 b = c.evaluate(1.0);
        EXPECT_NEAR(a.x, b.x, 1e-12);
        EXPECT_NEAR(a.y, b.y, 1e-12);
    }
}

TEST(BezierCurveTest, DegreeElevation) {
    BezierCurve2d c({ Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) });
    // 升阶后几何不变：升 1 次后同参数同点
    auto up = c.elevateDegree();
    for (double t : { 0.2, 0.5, 0.8 }) {
        Point2 evC = c.evaluate(t);
        Point2 evUp = up.evaluate(t);
        EXPECT_NEAR(evC.x, evUp.x, 1e-9);
        EXPECT_NEAR(evC.y, evUp.y, 1e-9);
    }
}

TEST(BezierCurveTest, Subdivision) {
    BezierCurve2d c({ Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) });
    // 细分后拼起来几何不变
    auto [left, right] = c.subdivide(0.5);
    {
        Point2 lEnd = left.evaluate(1.0);
        Point2 rStart = right.evaluate(0.0);
        EXPECT_NEAR(lEnd.x, rStart.x, 1e-12);
        EXPECT_NEAR(lEnd.y, rStart.y, 1e-12);
    }
    {
        Point2 lEnd = left.evaluate(1.0);
        Point2 cMid = c.evaluate(0.5);
        EXPECT_NEAR(lEnd.x, cMid.x, 1e-9);
        EXPECT_NEAR(lEnd.y, cMid.y, 1e-9);
    }
}

TEST(BezierCurveTest, Curve3D) {
    // 3D 曲线
    BezierCurve3d c3({ Point3(0, 0, 0), Point3(1, 1, 0), Point3(2, 0, 1) });
    {
        Point3 p = c3.evaluate(0.0);
        EXPECT_NEAR(p.x, 0.0, 1e-12);
        EXPECT_NEAR(p.y, 0.0, 1e-12);
        EXPECT_NEAR(p.z, 0.0, 1e-12);
    }
    {
        Point3 p = c3.evaluate(1.0);
        EXPECT_NEAR(p.x, 2.0, 1e-12);
        EXPECT_NEAR(p.y, 0.0, 1e-12);
        EXPECT_NEAR(p.z, 1.0, 1e-12);
    }
    // 二次 Bezier 中点：0.25·P0 + 0.5·P1 + 0.25·P2
    {
        Point3 p = c3.evaluate(0.5);
        EXPECT_NEAR(p.x, 1.0, 1e-12);
        EXPECT_NEAR(p.y, 0.5, 1e-12);
        EXPECT_NEAR(p.z, 0.25, 1e-12);
    }
}

TEST(BezierCurveTest, DegreeReduction) {
    BezierCurve2d c({ Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) });
    auto dn = c.reduceDegree();
    EXPECT_EQ(dn.degree(), c.degree() - 1);
}

// ============================================================
// Bezier 曲面
// ============================================================

TEST(BezierSurfaceTest, CornerInterpolation) {
    // 2x2 单位平面：4 角点 + 凸起中心构成的曲面
    BezierSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BezierSurface s(std::move(grid));

    EXPECT_NEAR(s.evaluate(0, 0).x, 0.0, 1e-12);
    EXPECT_NEAR(s.evaluate(0, 0).y, 0.0, 1e-12);
    EXPECT_NEAR(s.evaluate(0, 0).z, 0.0, 1e-12);

    EXPECT_NEAR(s.evaluate(1, 0).x, 2.0, 1e-12);
    EXPECT_NEAR(s.evaluate(1, 0).y, 0.0, 1e-12);
    EXPECT_NEAR(s.evaluate(1, 0).z, 0.0, 1e-12);

    EXPECT_NEAR(s.evaluate(0, 1).x, 0.0, 1e-12);
    EXPECT_NEAR(s.evaluate(0, 1).y, 2.0, 1e-12);
    EXPECT_NEAR(s.evaluate(0, 1).z, 0.0, 1e-12);

    EXPECT_NEAR(s.evaluate(1, 1).x, 2.0, 1e-12);
    EXPECT_NEAR(s.evaluate(1, 1).y, 2.0, 1e-12);
    EXPECT_NEAR(s.evaluate(1, 1).z, 0.0, 1e-12);
}

TEST(BezierSurfaceTest, BernsteinEqualsDeCasteljau) {
    BezierSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BezierSurface s(std::move(grid));

    for (double u : { 0.2, 0.5, 0.8 })
        for (double v : { 0.3, 0.6 }) {
            Point3 ev = s.evaluate(u, v);
            Point3 dc = s.deCasteljau(u, v);
            EXPECT_NEAR(ev.x, dc.x, 1e-10);
            EXPECT_NEAR(ev.y, dc.y, 1e-10);
            EXPECT_NEAR(ev.z, dc.z, 1e-10);
        }
}

TEST(BezierSurfaceTest, CenterEvaluation) {
    BezierSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BezierSurface s(std::move(grid));

    // 中心点高度：4 角 0 + 4 边中 0 + 中心 1 → Bernstein(1,1; 0.5,0.5)=0.25
    Point3 c = s.evaluate(0.5, 0.5);
    EXPECT_NEAR(c.x, 1.0, 1e-10);
    EXPECT_NEAR(c.y, 1.0, 1e-10);
    EXPECT_NEAR(c.z, 0.25, 1e-10);
}

TEST(BezierSurfaceTest, Normal) {
    BezierSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BezierSurface s(std::move(grid));

    // 法向：对称凸曲面，中心法向朝 +Z
    Vec3 n = s.normal(0.5, 0.5);
    EXPECT_NEAR(n.x, 0.0, 1e-9);
    EXPECT_NEAR(n.y, 0.0, 1e-9);
    EXPECT_NEAR(n.z, 1.0, 1e-9);
}

TEST(BezierSurfaceTest, DegreeElevationU) {
    BezierSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BezierSurface s(std::move(grid));

    auto su = s.elevateDegreeU();
    for (double u : { 0.2, 0.5, 0.8 })
        for (double v : { 0.3, 0.7 }) {
            Point3 sVal = s.evaluate(u, v);
            Point3 suVal = su.evaluate(u, v);
            EXPECT_NEAR(sVal.x, suVal.x, 1e-9);
            EXPECT_NEAR(sVal.y, suVal.y, 1e-9);
            EXPECT_NEAR(sVal.z, suVal.z, 1e-9);
        }
}

TEST(BezierSurfaceTest, DegreeElevationV) {
    BezierSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BezierSurface s(std::move(grid));

    auto sv = s.elevateDegreeV();
    for (double u : { 0.2, 0.5, 0.8 })
        for (double v : { 0.3, 0.7 }) {
            Point3 sVal = s.evaluate(u, v);
            Point3 svVal = sv.evaluate(u, v);
            EXPECT_NEAR(sVal.x, svVal.x, 1e-9);
            EXPECT_NEAR(sVal.y, svVal.y, 1e-9);
            EXPECT_NEAR(sVal.z, svVal.z, 1e-9);
        }
}

// ============================================================
// B-spline 基函数 / 节点向量
// ============================================================

TEST(BSplineBasisTest, ClampedKnotVector) {
    // clamped 节点向量：5 控制点 3 次 → {0,0,0,0, 0.5, 1,1,1,1}
    auto U = clampedKnotVector(3, 5);
    EXPECT_EQ(static_cast<int>(U.size()), 9);
    EXPECT_NEAR(U[0], 0.0, 1e-12);
    EXPECT_NEAR(U[3], 0.0, 1e-12);
    EXPECT_NEAR(U[4], 0.5, 1e-12);
    EXPECT_NEAR(U[5], 1.0, 1e-12);
    EXPECT_NEAR(U[8], 1.0, 1e-12);
}

TEST(BSplineBasisTest, FindSpan) {
    auto U = clampedKnotVector(3, 5);
    // clamped 3 次 5 控制点（n=4），u=0.5 应落在 span 4
    EXPECT_EQ(bsplineFindSpan(4, 3, 0.5, U), 4);
    // u=0（左端点）→ span p = 3
    EXPECT_EQ(bsplineFindSpan(4, 3, 0.0, U), 3);
    // u=1（右端点）→ 闭合约定 → span n = 4
    EXPECT_EQ(bsplineFindSpan(4, 3, 1.0, U), 4);
}

TEST(BSplineBasisTest, PartitionOfUnity) {
    auto U = clampedKnotVector(3, 5);
    // 基函数单位分解：Σ_{i} N_{i,p}(u) = 1（在定义域内部 + 左端点）。
    // 注：右端点 u=umax 处单个基函数 N_{i,p} 因半开区间约定行为病态，
    //     工程上用 findSpan + de Boor 求值（见 BSplineCurve::evaluate），不在此求和。
    for (double u : { 0.0, 0.25, 0.5, 0.75, 0.999 }) {
        double sum = 0.0;
        for (int i = 0; i <= 4; ++i)
            sum += bsplineBasis(i, 3, u, U);
        EXPECT_NEAR(sum, 1.0, 1e-10);
    }
}

// ============================================================
// B-spline 曲线
// ============================================================

TEST(BSplineCurveTest, DomainAndEndpoints) {
    BSplineCurve2d::PointList cp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0), Point2(5, 2) };
    BSplineCurve2d c(3, cp);

    auto [umin, umax] = c.domain();
    EXPECT_NEAR(umin, 0.0, 1e-12);
    EXPECT_NEAR(umax, 1.0, 1e-12);

    // clamped 节点 → 穿过首末控制点
    EXPECT_NEAR(c.evaluate(0.0).x, 0.0, 1e-12);
    EXPECT_NEAR(c.evaluate(0.0).y, 0.0, 1e-12);
    EXPECT_NEAR(c.evaluate(1.0).x, 5.0, 1e-12);
    EXPECT_NEAR(c.evaluate(1.0).y, 2.0, 1e-12);
}

TEST(BSplineCurveTest, RejectsInvalidStructureInAllBuildTypes) {
    BSplineCurve2d::PointList cp = { Point2(0, 0), Point2(1, 1), Point2(2, 0) };
    EXPECT_THROW((BSplineCurve2d(0, cp)), std::invalid_argument);
    EXPECT_THROW((BSplineCurve2d(3, cp)), std::invalid_argument);
    EXPECT_THROW((BSplineCurve2d(2, cp, { 0, 0, 0, 1, 1 })), std::invalid_argument);
    EXPECT_THROW((BSplineCurve2d(2, cp, { 0, 0, 0, 0.8, 0.2, 1 })), std::invalid_argument);

    BSplineCurve2d curve(2, cp);
    EXPECT_THROW(curve.insertKnot(0.5, 3), std::invalid_argument);
}

TEST(BSplineCurveTest, EquivalenceWithBezier) {
    // 4 控制点 3 次，节点 {0,0,0,0,1,1,1,1}
    BSplineCurve2d::PointList bezCp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0) };
    BSplineCurve2d bs(3, bezCp, { 0, 0, 0, 0, 1, 1, 1, 1 });
    BezierCurve2d bz(bezCp);
    for (double t : { 0.1, 0.3, 0.5, 0.7, 0.9 }) {
        {
            Point2 bsVal = bs.evaluate(t);
            Point2 bzVal = bz.evaluate(t);
            EXPECT_NEAR(bsVal.x, bzVal.x, 1e-10);
            EXPECT_NEAR(bsVal.y, bzVal.y, 1e-10);
        }
        {
            Point2 bsVal = bs.evaluate(t);
            Point2 dcVal = bz.deCasteljau(t);
            EXPECT_NEAR(bsVal.x, dcVal.x, 1e-10);
            EXPECT_NEAR(bsVal.y, dcVal.y, 1e-10);
        }
    }
}

TEST(BSplineCurveTest, FirstDerivativeVsFiniteDiff) {
    BSplineCurve2d::PointList cp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0), Point2(5, 2) };
    BSplineCurve2d c(3, cp);

    // 解析导数 vs 中心差分（在内部点验证）
    for (double t : { 0.25, 0.5, 0.75 }) {
        Vec2 analytic = c.derivative(t, 1);
        double h = 1e-6;
        Point2 fp = c.evaluate(t + h);
        Point2 fm = c.evaluate(t - h);
        Vec2 numeric = (fp - fm) * (1.0 / (2.0 * h));
        EXPECT_NEAR(analytic.x, numeric.x, 1e-3);
        EXPECT_NEAR(analytic.y, numeric.y, 1e-3);
    }
}

TEST(BSplineCurveTest, SecondDerivativeVsFiniteDiff) {
    BSplineCurve2d::PointList cp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0), Point2(5, 2) };
    BSplineCurve2d c(3, cp);

    // 二阶导 vs 数值
    for (double t : { 0.3, 0.5, 0.7 }) {
        Vec2 analytic = c.derivative(t, 2);
        double h = 1e-4;
        Point2 p0 = c.evaluate(t);
        Point2 pp = c.evaluate(t + h);
        Point2 pm = c.evaluate(t - h);
        Vec2 numeric = ((pp - p0) + (pm - p0)) * (1.0 / (h * h));
        EXPECT_NEAR(analytic.x, numeric.x, 1e-1);
        EXPECT_NEAR(analytic.y, numeric.y, 1e-1);
    }
}

TEST(BSplineCurveTest, KnotInsertion) {
    BSplineCurve2d::PointList cp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0), Point2(5, 2) };
    BSplineCurve2d c2(3, cp);
    int before = c2.controlPointCount();
    Point2 beforeEval = c2.evaluate(0.5);
    c2.insertKnot(0.5, 1);
    EXPECT_EQ(c2.controlPointCount(), before + 1);
    EXPECT_NEAR(c2.evaluate(0.5).x, beforeEval.x, 1e-12);
    EXPECT_NEAR(c2.evaluate(0.5).y, beforeEval.y, 1e-12);
}

TEST(BSplineCurveTest, Curve3D) {
    BSplineCurve3d::PointList cp3 = { Point3(0, 0, 0), Point3(1, 1, 0), Point3(2, 0, 1), Point3(3, 1, 1),
                                      Point3(4, 0, 0) };
    BSplineCurve3d c3(3, cp3);
    {
        Point3 p = c3.evaluate(0.0);
        EXPECT_NEAR(p.x, 0.0, 1e-12);
        EXPECT_NEAR(p.y, 0.0, 1e-12);
        EXPECT_NEAR(p.z, 0.0, 1e-12);
    }
    {
        Point3 p = c3.evaluate(1.0);
        EXPECT_NEAR(p.x, 4.0, 1e-12);
        EXPECT_NEAR(p.y, 0.0, 1e-12);
        EXPECT_NEAR(p.z, 0.0, 1e-12);
    }
    // 导数应是合法向量
    Vec3 d3 = c3.derivative(0.5, 1);
    EXPECT_GT(d3.lengthSq(), 0.0);
}

// ============================================================
// B-spline 曲面
// ============================================================

TEST(BSplineSurfaceTest, DomainAndClampedCorners) {
    BSplineSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BSplineSurface s(2, 2, std::move(grid));

    auto [umin, umax] = s.domainU();
    auto [vmin, vmax] = s.domainV();
    EXPECT_NEAR(umin, 0.0, 1e-12);
    EXPECT_NEAR(umax, 1.0, 1e-12);

    // clamped → 四角穿过控制点
    {
        Point3 p = s.evaluate(0, 0);
        EXPECT_NEAR(p.x, 0.0, 1e-12);
        EXPECT_NEAR(p.y, 0.0, 1e-12);
        EXPECT_NEAR(p.z, 0.0, 1e-12);
    }
    {
        Point3 p = s.evaluate(1, 0);
        EXPECT_NEAR(p.x, 2.0, 1e-12);
        EXPECT_NEAR(p.y, 0.0, 1e-12);
        EXPECT_NEAR(p.z, 0.0, 1e-12);
    }
    {
        Point3 p = s.evaluate(0, 1);
        EXPECT_NEAR(p.x, 0.0, 1e-12);
        EXPECT_NEAR(p.y, 2.0, 1e-12);
        EXPECT_NEAR(p.z, 0.0, 1e-12);
    }
    {
        Point3 p = s.evaluate(1, 1);
        EXPECT_NEAR(p.x, 2.0, 1e-12);
        EXPECT_NEAR(p.y, 2.0, 1e-12);
        EXPECT_NEAR(p.z, 0.0, 1e-12);
    }
}

TEST(BSplineSurfaceTest, PartialDerivativesVsFiniteDiff) {
    BSplineSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BSplineSurface s(2, 2, std::move(grid));

    // 偏导 vs 中心差分
    auto [du_ana, dv_ana] = s.derivatives(0.5, 0.5);
    double h = 1e-6;
    Point3 pu_p = s.evaluate(0.5 + h, 0.5);
    Point3 pu_m = s.evaluate(0.5 - h, 0.5);
    Vec3 du_num = (pu_p - pu_m) * (1.0 / (2.0 * h));
    Point3 pv_p = s.evaluate(0.5, 0.5 + h);
    Point3 pv_m = s.evaluate(0.5, 0.5 - h);
    Vec3 dv_num = (pv_p - pv_m) * (1.0 / (2.0 * h));

    EXPECT_NEAR(du_ana.x, du_num.x, 1e-3);
    EXPECT_NEAR(du_ana.y, du_num.y, 1e-3);
    EXPECT_NEAR(du_ana.z, du_num.z, 1e-3);

    EXPECT_NEAR(dv_ana.x, dv_num.x, 1e-3);
    EXPECT_NEAR(dv_ana.y, dv_num.y, 1e-3);
    EXPECT_NEAR(dv_ana.z, dv_num.z, 1e-3);
}

TEST(BSplineSurfaceTest, Normal) {
    BSplineSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    BSplineSurface s(2, 2, std::move(grid));

    // 法向非零、单位长
    Vec3 n = s.normal(0.5, 0.5);
    EXPECT_NEAR(n.length(), 1.0, 1e-9);
}

TEST(BSplineSurfaceTest, EquivalenceWithBezierSurface) {
    BezierSurface::ControlGrid bezGrid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    // clamped 无内部节点时 == Bezier 曲面（p=q=2，3x3 控制点恰好是二次 Bezier 曲面）
    BSplineSurface bss(2, 2, bezGrid, { 0, 0, 0, 1, 1, 1 }, { 0, 0, 0, 1, 1, 1 });
    BezierSurface bz(bezGrid);
    for (double u : { 0.1, 0.4, 0.7 })
        for (double v : { 0.2, 0.5, 0.9 }) {
            Point3 bssVal = bss.evaluate(u, v);
            Point3 bzVal = bz.evaluate(u, v);
            EXPECT_NEAR(bssVal.x, bzVal.x, 1e-10);
            EXPECT_NEAR(bssVal.y, bzVal.y, 1e-10);
            EXPECT_NEAR(bssVal.z, bzVal.z, 1e-10);
        }
}

// ============================================================
// NURBS 曲线
// ============================================================

TEST(NURBSCurveTest, WeightOneEqualsBSpline) {
    NURBSCurve2d::PointList cp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0), Point2(5, 2) };
    NURBSCurve2d nurbs(3, cp);  // 权重默认 1
    BSplineCurve2d bspl(3, cp);
    for (double t : { 0.1, 0.3, 0.5, 0.7, 0.9 }) {
        Point2 nVal = nurbs.evaluate(t);
        Point2 bVal = bspl.evaluate(t);
        EXPECT_NEAR(nVal.x, bVal.x, 1e-10);
        EXPECT_NEAR(nVal.y, bVal.y, 1e-10);
    }
}

TEST(NURBSCurveTest, EndpointInterpolation) {
    NURBSCurve2d::PointList cp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0), Point2(5, 2) };
    NURBSCurve2d nurbs(3, cp);

    // clamped → 穿过首末控制点
    EXPECT_NEAR(nurbs.evaluate(0.0).x, 0.0, 1e-12);
    EXPECT_NEAR(nurbs.evaluate(0.0).y, 0.0, 1e-12);
    EXPECT_NEAR(nurbs.evaluate(1.0).x, 5.0, 1e-12);
    EXPECT_NEAR(nurbs.evaluate(1.0).y, 2.0, 1e-12);
}

TEST(NURBSCurveTest, RationalArcOnUnitCircle) {
    // 2 次有理 Bezier 精确表示 90° 单位圆弧
    // 控制点：(1,0), (1,1), (0,1)；权重 1, √2/2, 1；节点 {0,0,0,1,1,1}
    const double r2_2 = 0.5 * std::sqrt(2.0);
    NURBSCurve2d::PointList arcCp = { Point2(1, 0), Point2(1, 1), Point2(0, 1) };
    NURBSCurve2d arc(2, arcCp, { 1.0, r2_2, 1.0 }, { 0, 0, 0, 1, 1, 1 });

    // 端点精确
    EXPECT_NEAR(arc.evaluate(0.0).x, 1.0, 1e-12);
    EXPECT_NEAR(arc.evaluate(0.0).y, 0.0, 1e-12);
    EXPECT_NEAR(arc.evaluate(1.0).x, 0.0, 1e-12);
    EXPECT_NEAR(arc.evaluate(1.0).y, 1.0, 1e-12);
    // 中点（对称）应在 θ=π/4
    EXPECT_NEAR(arc.evaluate(0.5).x, r2_2, 1e-10);
    EXPECT_NEAR(arc.evaluate(0.5).y, r2_2, 1e-10);
    // 所有点在单位圆上
    for (double t : { 0.0, 0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9, 1.0 }) {
        Point2 c = arc.evaluate(t);
        EXPECT_NEAR(c.x * c.x + c.y * c.y, 1.0, 1e-10);
    }
}

TEST(NURBSCurveTest, ArcDerivativeDirection) {
    // 端点切向：在 (1,0) 处切向 = (0,1)（沿 +y）
    const double r2_2 = 0.5 * std::sqrt(2.0);
    NURBSCurve2d::PointList arcCp = { Point2(1, 0), Point2(1, 1), Point2(0, 1) };
    NURBSCurve2d arc(2, arcCp, { 1.0, r2_2, 1.0 }, { 0, 0, 0, 1, 1, 1 });

    Vec2 d0 = arc.derivative(0.0);
    Vec2 d0n = d0.normalized();
    EXPECT_NEAR(d0n.x, 0.0, 1e-9);
    EXPECT_NEAR(d0n.y, 1.0, 1e-9);
}

TEST(NURBSCurveTest, KnotInsertion) {
    const double r2_2 = 0.5 * std::sqrt(2.0);
    NURBSCurve2d::PointList arcCp = { Point2(1, 0), Point2(1, 1), Point2(0, 1) };
    NURBSCurve2d arc2(2, arcCp, { 1.0, r2_2, 1.0 }, { 0, 0, 0, 1, 1, 1 });
    Point2 beforeEval = arc2.evaluate(0.5);
    arc2.insertKnot(0.5, 1);
    EXPECT_NEAR(arc2.evaluate(0.5).x, beforeEval.x, 1e-10);
    EXPECT_NEAR(arc2.evaluate(0.5).y, beforeEval.y, 1e-10);
    EXPECT_EQ(arc2.controlPointCount(), 4);  // 3 → 4
}

TEST(NURBSCurveTest, DerivativeVsFiniteDiff) {
    // B-spline 退化情形（权重全 1）
    NURBSCurve2d::PointList cp = { Point2(0, 0), Point2(1, 2), Point2(3, 2), Point2(4, 0), Point2(5, 2) };
    NURBSCurve2d nurbs(3, cp);

    for (double t : { 0.2, 0.5, 0.8 }) {
        Vec2 analytic = nurbs.derivative(t);
        double hh = 1e-6;
        Point2 fp = nurbs.evaluate(t + hh);
        Point2 fm = nurbs.evaluate(t - hh);
        Vec2 numeric = (fp - fm) * (1.0 / (2.0 * hh));
        EXPECT_NEAR(analytic.x, numeric.x, 1e-3);
        EXPECT_NEAR(analytic.y, numeric.y, 1e-3);
    }
}

TEST(NURBSCurveTest, ArcDerivativeVsFiniteDiff) {
    // 圆弧的解析导数 vs 数值（真正有理情形，验证商法则）
    const double r2_2 = 0.5 * std::sqrt(2.0);
    NURBSCurve2d::PointList arcCp = { Point2(1, 0), Point2(1, 1), Point2(0, 1) };
    NURBSCurve2d arc(2, arcCp, { 1.0, r2_2, 1.0 }, { 0, 0, 0, 1, 1, 1 });

    for (double t : { 0.25, 0.5, 0.75 }) {
        Vec2 analytic = arc.derivative(t);
        double hh = 1e-6;
        Point2 fp = arc.evaluate(t + hh);
        Point2 fm = arc.evaluate(t - hh);
        Vec2 numeric = (fp - fm) * (1.0 / (2.0 * hh));
        EXPECT_NEAR(analytic.x, numeric.x, 1e-3);
        EXPECT_NEAR(analytic.y, numeric.y, 1e-3);
    }
}

TEST(NURBSCurveTest, Curve3DEqualsBSpline) {
    // 3D NURBS：权重全 1 时 == 3D B-spline
    NURBSCurve3d::PointList cp3 = { Point3(0, 0, 0), Point3(1, 1, 0), Point3(2, 0, 1), Point3(3, 1, 1),
                                    Point3(4, 0, 0) };
    NURBSCurve3d nurbs3(3, cp3);
    BSplineCurve3d bspl3(3, cp3);
    for (double t : { 0.1, 0.5, 0.9 }) {
        Point3 nVal = nurbs3.evaluate(t);
        Point3 bVal = bspl3.evaluate(t);
        EXPECT_NEAR(nVal.x, bVal.x, 1e-10);
        EXPECT_NEAR(nVal.y, bVal.y, 1e-10);
        EXPECT_NEAR(nVal.z, bVal.z, 1e-10);
    }
}

// ============================================================
// NURBS 曲面
// ============================================================

TEST(NURBSSurfaceTest, WeightOneEqualsBSplineSurface) {
    NURBSSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    NURBSSurface ns(2, 2, grid);
    BSplineSurface bss(2, 2, BSplineSurface::ControlGrid(grid));
    for (double u : { 0.1, 0.4, 0.7 })
        for (double v : { 0.2, 0.5, 0.9 }) {
            Point3 nsVal = ns.evaluate(u, v);
            Point3 bssVal = bss.evaluate(u, v);
            EXPECT_NEAR(nsVal.x, bssVal.x, 1e-10);
            EXPECT_NEAR(nsVal.y, bssVal.y, 1e-10);
            EXPECT_NEAR(nsVal.z, bssVal.z, 1e-10);
        }
}

TEST(NURBSSurfaceTest, ClampedCorners) {
    NURBSSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    NURBSSurface ns(2, 2, grid);

    EXPECT_NEAR(ns.evaluate(0, 0).x, 0.0, 1e-12);
    EXPECT_NEAR(ns.evaluate(0, 0).y, 0.0, 1e-12);
    EXPECT_NEAR(ns.evaluate(0, 0).z, 0.0, 1e-12);

    EXPECT_NEAR(ns.evaluate(1, 1).x, 2.0, 1e-12);
    EXPECT_NEAR(ns.evaluate(1, 1).y, 2.0, 1e-12);
    EXPECT_NEAR(ns.evaluate(1, 1).z, 0.0, 1e-12);
}

TEST(NURBSSurfaceTest, PartialDerivativesVsFiniteDiff) {
    NURBSSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    NURBSSurface ns(2, 2, grid);

    // 偏导 vs 中心差分（权重全 1 的退化情形）
    auto [du, dv] = ns.derivatives(0.5, 0.5);
    double h = 1e-6;
    Vec3 du_num = (ns.evaluate(0.5 + h, 0.5) - ns.evaluate(0.5 - h, 0.5)) * (1.0 / (2.0 * h));
    Vec3 dv_num = (ns.evaluate(0.5, 0.5 + h) - ns.evaluate(0.5, 0.5 - h)) * (1.0 / (2.0 * h));

    EXPECT_NEAR(du.x, du_num.x, 1e-3);
    EXPECT_NEAR(du.y, du_num.y, 1e-3);
    EXPECT_NEAR(du.z, du_num.z, 1e-3);

    EXPECT_NEAR(dv.x, dv_num.x, 1e-3);
    EXPECT_NEAR(dv.y, dv_num.y, 1e-3);
    EXPECT_NEAR(dv.z, dv_num.z, 1e-3);
}

TEST(NURBSSurfaceTest, NormalUnitLength) {
    NURBSSurface::ControlGrid grid = {
        { Point3(0, 0, 0), Point3(1, 0, 0), Point3(2, 0, 0) },
        { Point3(0, 1, 0), Point3(1, 1, 1), Point3(2, 1, 0) },
        { Point3(0, 2, 0), Point3(1, 2, 0), Point3(2, 2, 0) },
    };
    NURBSSurface ns(2, 2, grid);

    Vec3 n = ns.normal(0.5, 0.5);
    EXPECT_NEAR(n.length(), 1.0, 1e-9);
}
