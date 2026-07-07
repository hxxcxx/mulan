/**
 * @file triangulation_tests.cpp
 * @brief 多边形三角剖分(Ear Clipping) + 点集 Delaunay 测试
 * @author hxxcxx
 * @date 2026-07-07
 *
 * GTest 移植。
 */
#include <gtest/gtest.h>
#include <mulan/math/math.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace mulan::math;

TEST(TriangulationTest, EarClippingSquare) {
    // 正方形 → 2 个三角形
    std::vector<Point2> sq = { Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) };
    auto r = triangulatePolygon(sq);
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.triangleCount() == 2);
    EXPECT_NEAR(r.totalArea(), 4.0, 1e-9);
    // 每个三角形非退化
    for (const auto& t : r.triangles) {
        EXPECT_TRUE(!t.isDegenerate());
    }
}

TEST(TriangulationTest, EarClippingCW) {
    // 顺时针输入也能正确处理
    std::vector<Point2> sq = { Point2(0, 0), Point2(0, 2), Point2(2, 2), Point2(2, 0) };
    auto r = triangulatePolygon(sq);
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.triangleCount() == 2);
    EXPECT_NEAR(r.totalArea(), 4.0, 1e-9);
}

TEST(TriangulationTest, EarClippingConcave) {
    // 凹多边形：L 形（5 顶点）→ 3 个三角形
    std::vector<Point2> L = { Point2(0, 0), Point2(2, 0), Point2(2, 1), Point2(1, 1), Point2(1, 2), Point2(0, 2) };
    auto r = triangulatePolygon(L);
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.triangleCount() == L.size() - 2);  // n-2 = 4
    // L 形面积 = 3
    EXPECT_NEAR(r.totalArea(), 3.0, 1e-9);
}

TEST(TriangulationTest, Triangle2Geom) {
    Triangle2 t(Point2(0, 0), Point2(2, 0), Point2(0, 2));
    EXPECT_NEAR(t.area(), 2.0, 1e-12);
    // 外心应为 (1,1)
    Point2 cc = t.circumcenter();
    EXPECT_NEAR(cc.x, 1.0, 1e-9);
    EXPECT_NEAR(cc.y, 1.0, 1e-9);
    EXPECT_NEAR(t.circumradius(), std::sqrt(2.0), 1e-9);
    EXPECT_TRUE(t.contains(Point2(0.5, 0.5)));
    EXPECT_FALSE(t.contains(Point2(2, 2)));
    // 退化三角形
    Triangle2 deg(Point2(0, 0), Point2(1, 1), Point2(2, 2));
    EXPECT_TRUE(deg.isDegenerate());
}

TEST(TriangulationTest, DelaunayGrid) {
    // 4 个点构成正方形 → Delaunay 应得 2 个三角形，面积=4
    std::vector<Point2> pts = { Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) };
    auto r = triangulateDelaunay(pts);
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.triangleCount() == 2);
    EXPECT_NEAR(r.totalArea(), 4.0, 1e-9);
    // 所有三角形非退化
    for (const auto& t : r.triangles) {
        EXPECT_TRUE(!t.isDegenerate());
    }
}

TEST(TriangulationTest, DelaunayCount) {
    // 一般位置 6 个点，凸包顶点 h 个，三角形数 = 2n - 2 - h
    // 正方形 4 顶点 + 中心 2 点，h=4, n=6 → 12-2-4=6
    std::vector<Point2> pts = {
        Point2(0, 0), Point2(4, 0), Point2(4, 4), Point2(0, 4), Point2(1.5, 2), Point2(2.5, 2)
    };
    auto r = triangulateDelaunay(pts);
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.triangleCount() == 6);
}

TEST(TriangulationTest, EarClippingDegenerate) {
    // 共线点（全部共线）→ 无有效三角形
    std::vector<Point2> line = { Point2(0, 0), Point2(1, 0), Point2(2, 0) };
    auto r = triangulatePolygon(line);
    EXPECT_TRUE(!r.isValid());  // 共线清理后 < 3 顶点
}
