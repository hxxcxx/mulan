/**
 * @file convex_hull_tests.cpp
 * @brief 凸包适配验证测试 — 验证 BeyondConvex→mulan::math 的整套适配模式
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 覆盖：
 *  - 三种算法（Jarvis/Graham/MonotoneChain）在正方形/含内点/共线点上的结果一致性
 *  - CCW 顺序、顶点数、面积、周长
 *  - contains 查询
 *  - 退化输入（< 3 点）返回空凸包
 *
 * 复用 math_tests.cpp 的 CHECK/CHECK_NEAR 宏风格。
 */
#include <mulan/math/math.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace mulan::math;

int g_failures = 0;

void fail(const char* expr, const char* file, int line, const std::string& message = {}) {
    ++g_failures;
    std::cerr << file << ':' << line << ": CHECK failed: " << expr;
    if (!message.empty()) {
        std::cerr << " (" << message << ')';
    }
    std::cerr << '\n';
}

#define CHECK(expr)                          \
    do {                                     \
        if (!(expr))                         \
            fail(#expr, __FILE__, __LINE__); \
    } while (false)

#define CHECK_NEAR(actual, expected, eps)                                              \
    do {                                                                               \
        const double av = static_cast<double>(actual);                                 \
        const double ev = static_cast<double>(expected);                               \
        const double ep = static_cast<double>(eps);                                    \
        if (std::abs(av - ev) > ep) {                                                  \
            fail(#actual " ~= " #expected, __FILE__, __LINE__,                         \
                 "actual=" + std::to_string(av) + ", expected=" + std::to_string(ev)); \
        }                                                                              \
    } while (false)

// ----- 测试用点集 -----

// 单位正方形 4 角（CCW）+ 中心内点
std::vector<Point2> squareWithInterior() {
    return { Point2(0, 0), Point2(1, 0), Point2(1, 1), Point2(0, 1), Point2(0.5, 0.5) };
}

// 正方形 + 共线边上的点（应被去除，凸包顶点数仍为 4）
std::vector<Point2> squareWithCollinearOnEdges() {
    return { Point2(0, 0), Point2(0.5, 0), Point2(1, 0),  // 底边共线
             Point2(1, 1), Point2(0, 1) };
}

// 退化为线段（2 点）
std::vector<Point2> degenerateSegment() {
    return { Point2(0, 0), Point2(1, 1) };
}

// 验证顶点集（顺序无关）等价
bool sameVertexSet(const ConvexHull& h, const std::vector<Point2>& expected, double eps = 1e-9) {
    if (h.size() != expected.size())
        return false;
    std::set<std::pair<double, double>> hullPts;
    for (const Point2& p : h.vertices())
        hullPts.insert({ p.x, p.y });
    for (const Point2& p : expected) {
        // 容差匹配：每个期望点在 hull 中找近邻
        bool found = false;
        for (const Point2& q : h.vertices()) {
            if (std::abs(p.x - q.x) <= eps && std::abs(p.y - q.y) <= eps) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

// 验证 CCW（面积符号为正）
bool isCCW(const ConvexHull& h) {
    const auto& v = h.vertices();
    double sum = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        const Point2& a = v[i];
        const Point2& b = v[(i + 1) % v.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum > 0.0;
}

// ----- 用例 -----

void testSquareWithInterior() {
    const auto pts = squareWithInterior();
    const std::vector<Point2> expectedCorners = { Point2(0, 0), Point2(1, 0), Point2(1, 1), Point2(0, 1) };

    for (auto algo :
         { ConvexHullAlgorithm::JarvisMarch, ConvexHullAlgorithm::GrahamScan, ConvexHullAlgorithm::MonotoneChain }) {
        ConvexHull h = convexHull(pts, algo);
        CHECK(h.size() == 4);
        CHECK(sameVertexSet(h, expectedCorners));
        CHECK(isCCW(h));
        CHECK_NEAR(h.area(), 1.0, 1e-9);
        CHECK_NEAR(h.perimeter(), 4.0, 1e-9);
        // 内点应在凸包内
        CHECK(h.contains(Point2(0.5, 0.5)));
        // 外点不在内
        CHECK(!h.contains(Point2(2.0, 2.0)));
    }
}

void testCollinearOnEdges() {
    const auto pts = squareWithCollinearOnEdges();
    const std::vector<Point2> expectedCorners = { Point2(0, 0), Point2(1, 0), Point2(1, 1), Point2(0, 1) };

    for (auto algo :
         { ConvexHullAlgorithm::JarvisMarch, ConvexHullAlgorithm::GrahamScan, ConvexHullAlgorithm::MonotoneChain }) {
        ConvexHull h = convexHull(pts, algo);
        CHECK(h.size() == 4);
        CHECK(sameVertexSet(h, expectedCorners));
        CHECK(isCCW(h));
    }
}

void testDegenerateInput() {
    // 少于 3 点 → 空凸包
    ConvexHull h = convexHull(degenerateSegment());
    CHECK(h.isEmpty());
    CHECK(h.size() == 0);
    CHECK(h.vertices().empty());
    CHECK(h.edges().empty());
    CHECK_NEAR(h.area(), 0.0, 1e-12);
    CHECK_NEAR(h.perimeter(), 0.0, 1e-12);
}

void testTriangle() {
    std::vector<Point2> tri = { Point2(0, 0), Point2(4, 0), Point2(0, 3) };
    for (auto algo :
         { ConvexHullAlgorithm::JarvisMarch, ConvexHullAlgorithm::GrahamScan, ConvexHullAlgorithm::MonotoneChain }) {
        ConvexHull h = convexHull(tri, algo);
        CHECK(h.size() == 3);
        CHECK(isCCW(h));
        CHECK_NEAR(h.area(), 6.0, 1e-9);        // 1/2 * 4 * 3
        CHECK_NEAR(h.perimeter(), 12.0, 1e-9);  // 4 + 3 + 5
    }
}

void testOrientation() {
    Point2 p(0, 0), q(1, 0);
    CHECK(orientation(p, q, Point2(0.5, 0.5)) == Orientation::Left);
    CHECK(orientation(p, q, Point2(0.5, -0.5)) == Orientation::Right);
    CHECK(orientation(p, q, Point2(0.5, 0.0)) == Orientation::Collinear);
    CHECK(toLeft(p, q, Point2(0.5, 0.5)));
    CHECK(toLeftOrOn(p, q, Point2(0.5, 0.0)));
    CHECK(!toLeft(p, q, Point2(0.5, 0.0)));
}

void testEdgesAndWraparound() {
    ConvexHull h = convexHull(squareWithInterior());
    // 4 条边
    CHECK(h.edges().size() == 4);
    // 环绕访问
    CHECK(h.vertexAt(0) == h.vertexAt(4));  // 取模
    // prev/next
    const Point2 v0 = h.vertices()[0];
    const Point2 v1 = h.vertices()[1];
    CHECK(h.next(0) == v1);
    CHECK(h.prev(1) == v0);
}

}  // namespace

int main() {
    testSquareWithInterior();
    testCollinearOnEdges();
    testDegenerateInput();
    testTriangle();
    testOrientation();
    testEdgesAndWraparound();

    if (g_failures == 0) {
        std::cout << "convex_hull_tests: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "convex_hull_tests: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
