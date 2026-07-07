/**
 * @file intersect_tests.cpp
 * @brief 线段求交 / 凸多边形求交 测试
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

TEST(IntersectTest, SegmentSegment) {
    // X 形相交
    Segment2 a(Point2(0, 0), Point2(2, 2));
    Segment2 b(Point2(0, 2), Point2(2, 0));
    Point2 p;
    EXPECT_TRUE(segmentsIntersect(a, b, nullptr, nullptr, &p));
    EXPECT_NEAR(p.x, 1.0, 1e-9);
    EXPECT_NEAR(p.y, 1.0, 1e-9);

    // 不相交（平行）
    Segment2 c(Point2(0, 0), Point2(1, 0));
    Segment2 d(Point2(0, 1), Point2(1, 1));
    EXPECT_FALSE(segmentsIntersect(c, d));

    // 端点相交（共享端点）
    Segment2 e(Point2(0, 0), Point2(1, 0));
    Segment2 f(Point2(1, 0), Point2(1, 1));
    Point2 p2;
    EXPECT_TRUE(segmentsIntersect(e, f, nullptr, nullptr, &p2));
    EXPECT_NEAR(p2.x, 1.0, 1e-9);
    EXPECT_NEAR(p2.y, 0.0, 1e-9);
}

TEST(IntersectTest, FindAllSegments) {
    // 两条十字线段，1 个交点
    std::vector<Segment2> segs = {
        Segment2(Point2(0, 0), Point2(2, 2)),
        Segment2(Point2(0, 2), Point2(2, 0)),
    };
    auto hits = findAllSegmentIntersections(segs);
    EXPECT_TRUE(hits.size() == 1);
    if (!hits.empty()) {
        EXPECT_TRUE(hits[0].segmentA == 0);
        EXPECT_TRUE(hits[0].segmentB == 1);
        EXPECT_NEAR(hits[0].point.x, 1.0, 1e-9);
    }

    // 三条线段两两相交，3 个交点
    std::vector<Segment2> star = {
        Segment2(Point2(0, 0), Point2(2, 2)),
        Segment2(Point2(0, 2), Point2(2, 0)),
        Segment2(Point2(0, 1), Point2(2, 1)),
    };
    auto hits2 = findAllSegmentIntersections(star);
    EXPECT_TRUE(hits2.size() == 3);
}

TEST(IntersectTest, ConvexPolygon) {
    // 两矩形部分重叠：a=[0,2]×[0,2], b=[1,3]×[0,2]（y 完全重叠）
    //   → 交集 [1,2]×[0,2]（1×2 矩形，面积=2）
    ConvexHull a = convexHull({ Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) });
    ConvexHull b = convexHull({ Point2(1, 0), Point2(3, 0), Point2(3, 2), Point2(1, 2) });
    auto r = convexPolygonIntersect(a, b);
    EXPECT_FALSE(r.isEmpty);
    EXPECT_TRUE(r.vertices.size() == 4);
    ConvexHull ih(r.vertices);
    EXPECT_NEAR(ih.area(), 2.0, 1e-9);

    // 完全分离 → 空
    ConvexHull c = convexHull({ Point2(10, 10), Point2(12, 10), Point2(12, 12), Point2(10, 12) });
    auto r2 = convexPolygonIntersect(a, c);
    EXPECT_TRUE(r2.isEmpty);

    // 包含关系：a 包含 d → 交集 = d
    ConvexHull d = convexHull({ Point2(0.5, 0.5), Point2(1, 0.5), Point2(1, 1), Point2(0.5, 1) });
    auto r3 = convexPolygonIntersect(a, d);
    EXPECT_FALSE(r3.isEmpty);
    EXPECT_TRUE(r3.vertices.size() == 4);
    EXPECT_NEAR(ConvexHull(r3.vertices).area(), d.area(), 1e-9);
}

TEST(IntersectTest, PointInPolygon) {
    std::vector<Point2> sq = { Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) };
    EXPECT_TRUE(pointInConvexPolygon(Point2(1, 1), sq));
    EXPECT_TRUE(pointInConvexPolygon(Point2(0, 0), sq));  // 顶点
    EXPECT_TRUE(pointInConvexPolygon(Point2(1, 0), sq));  // 边上
    EXPECT_FALSE(pointInConvexPolygon(Point2(3, 1), sq));
    EXPECT_FALSE(pointInConvexPolygon(Point2(-1, 1), sq));
}
