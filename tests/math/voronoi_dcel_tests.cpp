/**
 * @file voronoi_dcel_tests.cpp
 * @brief Voronoi 图 + DCEL 拓扑结构测试
 * @author hxxcxx
 * @date 2026-07-07
 */
#include <mulan/math/math.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace mulan::math;

// ---------- Voronoi ----------

TEST(VoronoiTest, TwoSites) {
    // 两点 Voronoi：左 site 的单元在左半平面，右 site 的单元在右半平面
    std::vector<Point2> sites = { Point2(-2, 0), Point2(2, 0) };
    VoronoiBounds b(-10, -10, 10, 10);
    auto r = voronoi(sites, b);
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.cellCount() == 2);
    // 两单元面积应近似相等（各占一半），总和 = 边界框面积 20×20=400
    double totalArea = 0;
    for (const auto& c : r.cells) {
        ConvexHull h(c.vertices);
        totalArea += h.area();
    }
    EXPECT_NEAR(totalArea, 400.0, 1e-6);
}

TEST(VoronoiTest, SingleSite) {
    std::vector<Point2> sites = { Point2(0, 0) };
    VoronoiBounds b(-5, -5, 5, 5);
    auto r = voronoi(sites, b);
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.cellCount() == 1);
    // 单点 → 整个边界框为一个单元
    EXPECT_TRUE(r.cells[0].vertexCount() == 4);
    EXPECT_NEAR(ConvexHull(r.cells[0].vertices).area(), 100.0, 1e-9);
}

TEST(VoronoiTest, FourCorners) {
    // 边界框四角放 site → 每个单元占 1/4
    std::vector<Point2> sites = { Point2(-4, -4), Point2(4, -4), Point2(4, 4), Point2(-4, 4) };
    VoronoiBounds b(-10, -10, 10, 10);
    auto r = voronoi(sites, b);
    EXPECT_TRUE(r.cellCount() == 4);
    // 各单元面积应 ≈ 100（总面积 400 / 4）
    for (const auto& c : r.cells) {
        EXPECT_TRUE(c.vertexCount() >= 3);
        EXPECT_NEAR(ConvexHull(c.vertices).area(), 100.0, 1e-6);
    }
}

TEST(VoronoiTest, SiteInOwnCell) {
    // 每个 site 必须落在自己的单元内
    std::vector<Point2> sites = { Point2(0, 0), Point2(3, 1), Point2(-2, 4), Point2(1, -3) };
    VoronoiBounds b(-20, -20, 20, 20);
    auto r = voronoi(sites, b);
    for (size_t i = 0; i < sites.size(); ++i) {
        // 用 ConvexHull 的 contains 判定（单元多边形为凸）
        ConvexHull h(r.cells[i].vertices);
        EXPECT_TRUE(h.contains(sites[i]));
    }
}

// ---------- DCEL ----------

TEST(DcelTest, Polygon) {
    DCEL d;
    Face* f = dcel_builder::buildPolygon({ Point2(0, 0), Point2(4, 0), Point2(4, 3), Point2(0, 3) }, &d);
    EXPECT_TRUE(f != nullptr);
    EXPECT_TRUE(d.vertexCount() == 4);
    EXPECT_TRUE(d.edgeCount() == 4);      // 4 条无向边
    EXPECT_TRUE(d.halfEdgeCount() == 8);  // 8 条半边
    EXPECT_TRUE(d.faceCount() == 1);
    EXPECT_TRUE(f->outerBoundarySize() == 4);
    EXPECT_TRUE(d.validate());

    // 外边界顶点应为输入顺序
    auto verts = f->outerBoundaryVertices();
    EXPECT_TRUE(verts.size() == 4);
    EXPECT_NEAR(verts[0]->coords().x, 0.0, 1e-12);
    EXPECT_NEAR(verts[1]->coords().x, 4.0, 1e-12);
}

TEST(DcelTest, BoundingBox) {
    DCEL d;
    Face* f = dcel_builder::buildBoundingBox(0, 0, 2, 2, &d);
    EXPECT_TRUE(d.validate());
    EXPECT_TRUE(f->outerBoundarySize() == 4);
}

TEST(DcelTest, Triangulation) {
    // 正方形剖成 2 个三角形
    DCEL d;
    std::vector<Point2> verts = { Point2(0, 0), Point2(2, 0), Point2(2, 2), Point2(0, 2) };
    std::vector<std::array<int, 3>> tris = { { 0, 1, 2 }, { 0, 2, 3 } };
    auto faces = dcel_builder::buildTriangulation(verts, tris, &d);
    EXPECT_TRUE(faces.size() == 2);
    EXPECT_TRUE(d.vertexCount() == 4);
    // 对角边 (0,2) 共享 → 5 条无向边（4 外 + 1 对角）
    EXPECT_TRUE(d.edgeCount() == 5);
    EXPECT_TRUE(d.validate());
    // 对角的 twin 配对应正确：找到 0->2 的半边，其 twin 的 origin 应为 2
    bool foundDiagonal = false;
    for (size_t i = 0; i < d.halfEdgeCount(); ++i) {
        HalfEdge* he = d.halfEdge(static_cast<int>(i));
        if (he->origin()->id() == 0 && he->destination()->id() == 2) {
            foundDiagonal = true;
            EXPECT_TRUE(he->twin()->origin()->id() == 2);
            EXPECT_TRUE(he->twin()->destination()->id() == 0);
        }
    }
    EXPECT_TRUE(foundDiagonal);
}

TEST(DcelTest, HalfEdgeTraversal) {
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
    EXPECT_TRUE(cur == start);
    EXPECT_TRUE(steps == 4);  // 四边形一圈 4 步
}

TEST(DcelTest, PolygonWithHole) {
    DCEL d;
    // 外 5×5 正方形 + 中间 1×1 方形洞
    Face* f = dcel_builder::buildPolygonWithHoles({ Point2(0, 0), Point2(5, 0), Point2(5, 5), Point2(0, 5) },
                                                  { { Point2(2, 2), Point2(3, 2), Point2(3, 3), Point2(2, 3) } }, &d);
    EXPECT_TRUE(f != nullptr);
    EXPECT_TRUE(f->hasHoles());
    EXPECT_TRUE(f->innerComponents().size() == 1);
    EXPECT_TRUE(d.validate());
}
