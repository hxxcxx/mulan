/**
 * @file voronoi_dcel_tests.cpp
 * @brief Voronoi 图 + DCEL 拓扑结构测试
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

// ---------- Voronoi ----------

void testVoronoiTwoSites() {
    // 两点 Voronoi：左 site 的单元在左半平面，右 site 的单元在右半平面
    std::vector<Point2> sites = { Point2(-2, 0), Point2(2, 0) };
    VoronoiBounds b(-10, -10, 10, 10);
    auto r = voronoi(sites, b);
    CHECK(r.isValid());
    CHECK(r.cellCount() == 2);
    // 两单元面积应近似相等（各占一半），总和 = 边界框面积 20×20=400
    double totalArea = 0;
    for (const auto& c : r.cells) {
        ConvexHull h(c.vertices);
        totalArea += h.area();
    }
    CHECK_NEAR(totalArea, 400.0, 1e-6);
}

void testVoronoiSingleSite() {
    std::vector<Point2> sites = { Point2(0, 0) };
    VoronoiBounds b(-5, -5, 5, 5);
    auto r = voronoi(sites, b);
    CHECK(r.isValid());
    CHECK(r.cellCount() == 1);
    // 单点 → 整个边界框为一个单元
    CHECK(r.cells[0].vertexCount() == 4);
    CHECK_NEAR(ConvexHull(r.cells[0].vertices).area(), 100.0, 1e-9);
}

void testVoronoiFourCorners() {
    // 边界框四角放 site → 每个单元占 1/4
    std::vector<Point2> sites = { Point2(-4, -4), Point2(4, -4), Point2(4, 4), Point2(-4, 4) };
    VoronoiBounds b(-10, -10, 10, 10);
    auto r = voronoi(sites, b);
    CHECK(r.cellCount() == 4);
    // 各单元面积应 ≈ 100（总面积 400 / 4）
    for (const auto& c : r.cells) {
        CHECK(c.vertexCount() >= 3);
        CHECK_NEAR(ConvexHull(c.vertices).area(), 100.0, 1e-6);
    }
}

void testVoronoiSiteInOwnCell() {
    // 每个 site 必须落在自己的单元内
    std::vector<Point2> sites = { Point2(0, 0), Point2(3, 1), Point2(-2, 4), Point2(1, -3) };
    VoronoiBounds b(-20, -20, 20, 20);
    auto r = voronoi(sites, b);
    for (size_t i = 0; i < sites.size(); ++i) {
        // 用 ConvexHull 的 contains 判定（单元多边形为凸）
        ConvexHull h(r.cells[i].vertices);
        CHECK(h.contains(sites[i]));
    }
}

// ---------- DCEL ----------

void testDcelPolygon() {
    DCEL d;
    Face* f = dcel_builder::buildPolygon({ Point2(0, 0), Point2(4, 0), Point2(4, 3), Point2(0, 3) }, &d);
    CHECK(f != nullptr);
    CHECK(d.vertexCount() == 4);
    CHECK(d.edgeCount() == 4);      // 4 条无向边
    CHECK(d.halfEdgeCount() == 8);  // 8 条半边
    CHECK(d.faceCount() == 1);
    CHECK(f->outerBoundarySize() == 4);
    CHECK(d.validate());

    // 外边界顶点应为输入顺序
    auto verts = f->outerBoundaryVertices();
    CHECK(verts.size() == 4);
    CHECK_NEAR(verts[0]->coords().x, 0.0, 1e-12);
    CHECK_NEAR(verts[1]->coords().x, 4.0, 1e-12);
}

void testDcelBoundingBox() {
    DCEL d;
    Face* f = dcel_builder::buildBoundingBox(0, 0, 2, 2, &d);
    CHECK(d.validate());
    CHECK(f->outerBoundarySize() == 4);
}

void testDcelTriangulation() {
    // 正方形剖成 2 个三角形
    DCEL d;
    std::vector<Point2> verts = { Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) };
    std::vector<std::array<int, 3>> tris = { { 0, 1, 2 }, { 0, 2, 3 } };
    auto faces = dcel_builder::buildTriangulation(verts, tris, &d);
    CHECK(faces.size() == 2);
    CHECK(d.vertexCount() == 4);
    // 对角边 (0,2) 共享 → 5 条无向边（4 外 + 1 对角）
    CHECK(d.edgeCount() == 5);
    CHECK(d.validate());
    // 对角的 twin 配对应正确：找到 0->2 的半边，其 twin 的 origin 应为 2
    bool foundDiagonal = false;
    for (size_t i = 0; i < d.halfEdgeCount(); ++i) {
        HalfEdge* he = d.halfEdge(static_cast<int>(i));
        if (he->origin()->id() == 0 && he->destination()->id() == 2) {
            foundDiagonal = true;
            CHECK(he->twin()->origin()->id() == 2);
            CHECK(he->twin()->destination()->id() == 0);
        }
    }
    CHECK(foundDiagonal);
}

void testDcelHalfEdgeTraversal() {
    DCEL d;
    dcel_builder::buildPolygon({ Point2(0, 0), Point2(1, 0), Point2(1, 1), Point2(0, 1) }, &d);
    // 取第一条半边，沿 next 走一圈应回到原点
    HalfEdge* start = d.halfEdge(0);
    HalfEdge* cur = start;
    int steps = 0;
    do {
        cur = cur->next();
        ++steps;
    } while (cur != start && steps < 100);
    CHECK(cur == start);
    CHECK(steps == 4);  // 四边形一圈 4 步
}

void testDcelPolygonWithHole() {
    DCEL d;
    // 外 5×5 正方形 + 中间 1×1 方形洞
    Face* f = dcel_builder::buildPolygonWithHoles({ Point2(0, 0), Point2(5, 0), Point2(5, 5), Point2(0, 5) },
                                                  { { Point2(2, 2), Point2(3, 2), Point2(3, 3), Point2(2, 3) } }, &d);
    CHECK(f != nullptr);
    CHECK(f->hasHoles());
    CHECK(f->innerComponents().size() == 1);
    CHECK(d.validate());
}

}  // namespace

int main() {
    testVoronoiTwoSites();
    testVoronoiSingleSite();
    testVoronoiFourCorners();
    testVoronoiSiteInOwnCell();
    testDcelPolygon();
    testDcelBoundingBox();
    testDcelTriangulation();
    testDcelHalfEdgeTraversal();
    testDcelPolygonWithHole();
    if (g_failures == 0) {
        std::cout << "voronoi_dcel_tests: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "voronoi_dcel_tests: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
