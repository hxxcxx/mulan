/**
 * @file spatial_tests.cpp
 * @brief 空间索引测试 — KD-Tree / Quadtree / R-Tree
 * @author hxxcxx
 * @date 2026-07-07
 */
#include <mulan/math/math.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
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

std::vector<Point2> samplePoints() {
    return { Point2(2, 3), Point2(5, 4), Point2(9, 6), Point2(4, 7), Point2(8, 1),
             Point2(7, 2), Point2(1, 8), Point2(6, 5), Point2(3, 9), Point2(0, 0) };
}

// ---------- KD-Tree ----------

void testKDTreeBuildQuery() {
    KDTree t;
    t.build(samplePoints());
    CHECK(t.size() == 10);
    CHECK(t.contains(Point2(5, 4)));
    CHECK(!t.contains(Point2(100, 100)));

    // 范围查询 [0,6]×[0,6]
    auto in = t.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    // 预期: (2,3)(5,4)(0,0)(6,5) — 共 4 个
    CHECK(in.size() == 4);

    // 半径查询 圆心(5,5) 半径 2
    auto rad = t.radiusQuery(Point2(5, 5), 2.0);
    // 距离<=2: (5,4)d1 (6,5)d~1.41 (5,4)... 检查每个返回点确实在半径内
    for (const Point2& p : rad) {
        CHECK(p.distance(Point2(5, 5)) <= 2.0 + 1e-9);
    }
    CHECK(rad.size() >= 1);
}

void testKDTreeNN() {
    KDTree t;
    t.build(samplePoints());
    Point2 nearest;
    CHECK(t.nearestNeighbor(Point2(4.9, 4.1), nearest));
    CHECK_NEAR(nearest.x, 5.0, 1e-9);
    CHECK_NEAR(nearest.y, 4.0, 1e-9);

    // KNN
    auto knn = t.kNearestNeighbors(Point2(5, 5), 3);
    CHECK(knn.size() == 3);
    // 距离应单调不减
    for (size_t i = 1; i < knn.size(); ++i) {
        CHECK(knn[i].distance(Point2(5, 5)) >= knn[i - 1].distance(Point2(5, 5)) - 1e-9);
    }
}

void testKDTreeInsertDup() {
    KDTree t;
    t.insert(Point2(1, 1));
    CHECK(!t.insert(Point2(1, 1)));  // 重复拒绝
    CHECK(t.size() == 1);
}

// ---------- Quadtree ----------

void testQuadtreeBasic() {
    Quadtree q(AABB2(Point2(0, 0), Point2(10, 10)), 2);
    for (const Point2& p : samplePoints())
        CHECK(q.insert(p));
    CHECK(q.size() == 10);
    CHECK(q.contains(Point2(5, 4)));
    CHECK(!q.contains(Point2(50, 50)));
    // 越界拒绝
    CHECK(!q.insert(Point2(100, 100)));
}

void testQuadtreeSubdivideInvariant() {
    // 8 个点挤在小区域，触发多次细分；修正后子节点不应超容
    Quadtree q(AABB2(Point2(0, 0), Point2(4, 4)), 2);
    for (int i = 0; i < 8; ++i) {
        q.insert(Point2(1.0 + i * 0.1, 1.0 + i * 0.1));
    }
    CHECK(q.size() == 8);
    // allPoints 应无重复（共 8 个）
    auto all = q.allPoints();
    CHECK(all.size() == 8);
    // NN 能找到最近
    Point2 nn;
    CHECK(q.nearestNeighbor(Point2(1.45, 1.45), nn));
}

void testQuadtreeRangeRemove() {
    Quadtree q(AABB2(Point2(0, 0), Point2(20, 20)), 3);
    for (const Point2& p : samplePoints())
        q.insert(p);
    auto in = q.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    for (const Point2& p : in) {
        CHECK(p.x >= 0 && p.x <= 6 && p.y >= 0 && p.y <= 6);
    }
    // remove
    CHECK(q.remove(Point2(5, 4)));
    CHECK(!q.contains(Point2(5, 4)));
    CHECK(q.size() == 9);
    CHECK(!q.remove(Point2(50, 50)));
}

void testQuadtreeKNN() {
    Quadtree q(AABB2(Point2(0, 0), Point2(20, 20)), 2);
    for (const Point2& p : samplePoints())
        q.insert(p);
    auto knn = q.kNearestNeighbors(Point2(5, 5), 3);
    CHECK(knn.size() == 3);
    for (size_t i = 1; i < knn.size(); ++i) {
        CHECK(knn[i].distance(Point2(5, 5)) >= knn[i - 1].distance(Point2(5, 5)) - 1e-9);
    }
}

// ---------- R-Tree ----------

void testRTreeInsertQuery() {
    RTree r;
    r.insert(AABB2(Point2(0, 0), Point2(2, 2)), 1);
    r.insert(AABB2(Point2(3, 3), Point2(5, 5)), 2);
    r.insert(AABB2(Point2(1, 4), Point2(4, 6)), 3);
    CHECK(r.size() == 3);
    CHECK(r.contains(1));

    // 点查询：点 (1,1) 只在矩形 1 内
    auto pp = r.pointQuery(Point2(1, 1));
    CHECK(pp.size() == 1);
    CHECK(pp[0] == 1);

    // 点 (3.5,4.5) 在矩形 2 和 3 内
    auto pp2 = r.pointQuery(Point2(3.5, 4.5));
    std::set<int> s2(pp2.begin(), pp2.end());
    CHECK(s2.count(2) == 1);
    CHECK(s2.count(3) == 1);

    // 范围查询：(2,2)-(4,4) 与矩形 1(角)、2、3 相交
    auto rq = r.rangeQuery(AABB2(Point2(2, 2), Point2(4, 4)));
    CHECK(rq.size() >= 2);  // 至少 2,3
}

void testRTreeRemove() {
    RTree r;
    for (int i = 1; i <= 5; ++i) {
        r.insert(AABB2(Point2(i, i), Point2(i + 1, i + 1)), i);
    }
    CHECK(r.size() == 5);
    CHECK(r.remove(3));
    CHECK(!r.contains(3));
    CHECK(r.size() == 4);
    // 删除后查询仍正确
    auto all = r.allData();
    CHECK(all.size() == 4);
    std::set<int> s(all.begin(), all.end());
    CHECK(s.count(3) == 0);
}

void testRTreeSplit() {
    // 插入超过 maxEntries 触发分裂，查询仍应全部命中
    RTree r(4, 2);
    for (int i = 0; i < 20; ++i) {
        r.insert(AABB2(Point2(i, i), Point2(i + 1, i + 1)), i);
    }
    CHECK(r.size() == 20);
    // 每个 ID 都能查到
    for (int i = 0; i < 20; ++i) {
        auto hit = r.pointQuery(Point2(i + 0.5, i + 0.5));
        CHECK(hit.size() == 1);
        CHECK(hit[0] == i);
    }
}

void testBSPTreeBasic() {
    BSPTree bs(AABB2(Point2(0, 0), Point2(10, 10)), 3);
    auto pts = samplePoints();
    for (const auto& p : pts)
        CHECK(bs.insert(p));
    CHECK(bs.size() == 10);
    CHECK(bs.contains(Point2(5, 4)));
    CHECK(!bs.contains(Point2(100, 100)));
    // 越界拒绝
    CHECK(!bs.insert(Point2(-1, -1)));

    // NN
    Point2 nearest;
    CHECK(bs.nearestNeighbor(Point2(4.9, 4.1), nearest));
    CHECK_NEAR(nearest.x, 5.0, 1e-9);

    // KNN
    auto knn = bs.kNearestNeighbors(Point2(5, 5), 3);
    CHECK(knn.size() == 3);

    // Range
    auto in = bs.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    CHECK(in.size() == 4);

    // Remove
    CHECK(bs.remove(Point2(5, 4)));
    CHECK(bs.size() == 9);
    CHECK(!bs.contains(Point2(5, 4)));
}

void testBVHTreeBasic() {
    BVHTree bv(AABB2(Point2(0, 0), Point2(10, 10)), 3);
    bv.build(samplePoints());
    CHECK(bv.size() == 10);
    CHECK(bv.contains(Point2(5, 4)));
    CHECK(!bv.contains(Point2(100, 100)));

    Point2 nearest;
    CHECK(bv.nearestNeighbor(Point2(4.9, 4.1), nearest));
    CHECK_NEAR(nearest.x, 5.0, 1e-9);

    auto knn = bv.kNearestNeighbors(Point2(5, 5), 3);
    CHECK(knn.size() == 3);

    auto in = bv.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    CHECK(in.size() == 4);

    // Insert incrementally
    CHECK(bv.insert(Point2(7, 8)));
    CHECK(bv.size() == 11);

    // Remove
    CHECK(bv.remove(Point2(5, 4)));
    CHECK(bv.size() == 10);
    CHECK(!bv.contains(Point2(5, 4)));
}

}  // namespace

int main() {
    testKDTreeBuildQuery();
    testKDTreeNN();
    testKDTreeInsertDup();
    testQuadtreeBasic();
    testQuadtreeSubdivideInvariant();
    testQuadtreeRangeRemove();
    testQuadtreeKNN();
    testRTreeInsertQuery();
    testRTreeRemove();
    testRTreeSplit();
    testBSPTreeBasic();
    testBVHTreeBasic();
    if (g_failures == 0) {
        std::cout << "spatial_tests: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "spatial_tests: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
