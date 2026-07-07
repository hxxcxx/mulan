/**
 * @file intersect_tests.cpp
 * @brief 线段求交 / 凸多边形求交 测试
 * @author hxxcxx
 * @date 2026-07-07
 */
#include <mulan/math/math.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace mulan::math;

int g_failures = 0;
void fail(const char* expr, const char* file, int line, const std::string& msg = {}) {
    ++g_failures;
    std::cerr << file << ':' << line << ": CHECK failed: " << expr;
    if (!msg.empty())
        std::cerr << " (" << msg << ')';
    std::cerr << '\n';
}
#define CHECK(expr)                          \
    do {                                     \
        if (!(expr))                         \
            fail(#expr, __FILE__, __LINE__); \
    } while (false)
#define CHECK_NEAR(actual, expected, eps)                                              \
    do {                                                                               \
        double av = double(actual), ev = double(expected), ep = double(eps);           \
        if (std::abs(av - ev) > ep)                                                    \
            fail(#actual " ~= " #expected, __FILE__, __LINE__,                         \
                 "actual=" + std::to_string(av) + ", expected=" + std::to_string(ev)); \
    } while (false)

void testSegmentsIntersect() {
    // X 形相交
    Segment2 a(Point2(0, 0), Point2(2, 2));
    Segment2 b(Point2(0, 2), Point2(2, 0));
    Point2 p;
    CHECK(segmentsIntersect(a, b, nullptr, nullptr, &p));
    CHECK_NEAR(p.x, 1.0, 1e-9);
    CHECK_NEAR(p.y, 1.0, 1e-9);

    // 不相交（平行）
    Segment2 c(Point2(0, 0), Point2(1, 0));
    Segment2 d(Point2(0, 1), Point2(1, 1));
    CHECK(!segmentsIntersect(c, d));

    // 端点相交（共享端点）
    Segment2 e(Point2(0, 0), Point2(1, 0));
    Segment2 f(Point2(1, 0), Point2(1, 1));
    Point2 p2;
    CHECK(segmentsIntersect(e, f, nullptr, nullptr, &p2));
    CHECK_NEAR(p2.x, 1.0, 1e-9);
    CHECK_NEAR(p2.y, 0.0, 1e-9);
}

void testFindAllIntersections() {
    // 两条十字线段，1 个交点
    std::vector<Segment2> segs = {
        Segment2(Point2(0, 0), Point2(2, 2)),
        Segment2(Point2(0, 2), Point2(2, 0)),
    };
    auto hits = findAllSegmentIntersections(segs);
    CHECK(hits.size() == 1);
    if (!hits.empty()) {
        CHECK(hits[0].segmentA == 0);
        CHECK(hits[0].segmentB == 1);
        CHECK_NEAR(hits[0].point.x, 1.0, 1e-9);
    }

    // 三条线段两两相交，3 个交点
    std::vector<Segment2> star = {
        Segment2(Point2(0, 0), Point2(2, 2)),
        Segment2(Point2(0, 2), Point2(2, 0)),
        Segment2(Point2(0, 1), Point2(2, 1)),
    };
    auto hits2 = findAllSegmentIntersections(star);
    CHECK(hits2.size() == 3);
}

void testConvexPolygonIntersect() {
    // 两矩形部分重叠：a=[0,2]×[0,2], b=[1,3]×[0,2]（y 完全重叠）
    //   → 交集 [1,2]×[0,2]（1×2 矩形，面积=2）
    ConvexHull a = convexHull({ Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) });
    ConvexHull b = convexHull({ Point2(1, 0), Point2(3, 0), Point2(3, 2), Point2(1, 2) });
    auto r = convexPolygonIntersect(a, b);
    CHECK(!r.isEmpty);
    CHECK(r.vertices.size() == 4);
    ConvexHull ih(r.vertices);
    CHECK_NEAR(ih.area(), 2.0, 1e-9);

    // 完全分离 → 空
    ConvexHull c = convexHull({ Point2(10, 10), Point2(12, 10), Point2(12, 12), Point2(10, 12) });
    auto r2 = convexPolygonIntersect(a, c);
    CHECK(r2.isEmpty);

    // 包含关系：a 包含 d → 交集 = d
    ConvexHull d = convexHull({ Point2(0.5, 0.5), Point2(1, 0.5), Point2(1, 1), Point2(0.5, 1) });
    auto r3 = convexPolygonIntersect(a, d);
    CHECK(!r3.isEmpty);
    CHECK(r3.vertices.size() == 4);
    CHECK_NEAR(ConvexHull(r3.vertices).area(), d.area(), 1e-9);
}

void testPointInConvexPolygon() {
    std::vector<Point2> sq = { Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) };
    CHECK(pointInConvexPolygon(Point2(1, 1), sq));
    CHECK(pointInConvexPolygon(Point2(0, 0), sq));  // 顶点
    CHECK(pointInConvexPolygon(Point2(1, 0), sq));  // 边上
    CHECK(!pointInConvexPolygon(Point2(3, 1), sq));
    CHECK(!pointInConvexPolygon(Point2(-1, 1), sq));
}

}  // namespace

int main() {
    testSegmentsIntersect();
    testFindAllIntersections();
    testConvexPolygonIntersect();
    testPointInConvexPolygon();
    if (g_failures == 0) {
        std::cout << "intersect_tests: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "intersect_tests: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
