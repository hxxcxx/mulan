/**
 * @file spatial_tests.cpp
 * @brief 空间索引测试 — KD-Tree / Quadtree / R-Tree
 * @author hxxcxx
 * @date 2026-07-07
 */
#include <mulan/math/math.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

using namespace mulan::math;

std::vector<Point2> samplePoints() {
    return { Point2(2, 3), Point2(5, 4), Point2(9, 6), Point2(4, 7), Point2(8, 1),
             Point2(7, 2), Point2(1, 8), Point2(6, 5), Point2(3, 9), Point2(0, 0) };
}

// ---------- KD-Tree ----------

TEST(KDTreeTest, BuildQuery) {
    KDTree t;
    t.build(samplePoints());
    EXPECT_TRUE(t.size() == 10);
    EXPECT_TRUE(t.contains(Point2(5, 4)));
    EXPECT_TRUE(!t.contains(Point2(100, 100)));

    // 范围查询 [0,6]×[0,6]
    auto in = t.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    // 预期: (2,3)(5,4)(0,0)(6,5) — 共 4 个
    EXPECT_TRUE(in.size() == 4);

    // 半径查询 圆心(5,5) 半径 2
    auto rad = t.radiusQuery(Point2(5, 5), 2.0);
    // 距离<=2: (5,4)d1 (6,5)d~1.41 (5,4)... 检查每个返回点确实在半径内
    for (const Point2& p : rad) {
        EXPECT_TRUE(p.distance(Point2(5, 5)) <= 2.0 + 1e-9);
    }
    EXPECT_TRUE(rad.size() >= 1);
}

TEST(KDTreeTest, NearestNeighbor) {
    KDTree t;
    t.build(samplePoints());
    Point2 nearest;
    EXPECT_TRUE(t.nearestNeighbor(Point2(4.9, 4.1), nearest));
    EXPECT_NEAR(nearest.x, 5.0, 1e-9);
    EXPECT_NEAR(nearest.y, 4.0, 1e-9);

    // KNN
    auto knn = t.kNearestNeighbors(Point2(5, 5), 3);
    EXPECT_TRUE(knn.size() == 3);
    // 距离应单调不减
    for (size_t i = 1; i < knn.size(); ++i) {
        EXPECT_TRUE(knn[i].distance(Point2(5, 5)) >= knn[i - 1].distance(Point2(5, 5)) - 1e-9);
    }
}

TEST(KDTreeTest, InsertDuplicate) {
    KDTree t;
    t.insert(Point2(1, 1));
    EXPECT_TRUE(!t.insert(Point2(1, 1)));  // 重复拒绝
    EXPECT_TRUE(t.size() == 1);
}

// ---------- Quadtree ----------

TEST(QuadtreeTest, Basic) {
    Quadtree q(AABB2(Point2(0, 0), Point2(10, 10)), 2);
    for (const Point2& p : samplePoints())
        EXPECT_TRUE(q.insert(p));
    EXPECT_TRUE(q.size() == 10);
    EXPECT_TRUE(q.contains(Point2(5, 4)));
    EXPECT_TRUE(!q.contains(Point2(50, 50)));
    // 越界拒绝
    EXPECT_TRUE(!q.insert(Point2(100, 100)));
}

TEST(QuadtreeTest, SubdivideInvariant) {
    // 8 个点挤在小区域，触发多次细分；修正后子节点不应超容
    Quadtree q(AABB2(Point2(0, 0), Point2(4, 4)), 2);
    for (int i = 0; i < 8; ++i) {
        q.insert(Point2(1.0 + i * 0.1, 1.0 + i * 0.1));
    }
    EXPECT_TRUE(q.size() == 8);
    // allPoints 应无重复（共 8 个）
    auto all = q.allPoints();
    EXPECT_TRUE(all.size() == 8);
    // NN 能找到最近
    Point2 nn;
    EXPECT_TRUE(q.nearestNeighbor(Point2(1.45, 1.45), nn));
}

TEST(QuadtreeTest, RangeRemove) {
    Quadtree q(AABB2(Point2(0, 0), Point2(20, 20)), 3);
    for (const Point2& p : samplePoints())
        q.insert(p);
    auto in = q.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    for (const Point2& p : in) {
        EXPECT_TRUE(p.x >= 0 && p.x <= 6 && p.y >= 0 && p.y <= 6);
    }
    // remove
    EXPECT_TRUE(q.remove(Point2(5, 4)));
    EXPECT_TRUE(!q.contains(Point2(5, 4)));
    EXPECT_TRUE(q.size() == 9);
    EXPECT_TRUE(!q.remove(Point2(50, 50)));
}

TEST(QuadtreeTest, KNN) {
    Quadtree q(AABB2(Point2(0, 0), Point2(20, 20)), 2);
    for (const Point2& p : samplePoints())
        q.insert(p);
    auto knn = q.kNearestNeighbors(Point2(5, 5), 3);
    EXPECT_TRUE(knn.size() == 3);
    for (size_t i = 1; i < knn.size(); ++i) {
        EXPECT_TRUE(knn[i].distance(Point2(5, 5)) >= knn[i - 1].distance(Point2(5, 5)) - 1e-9);
    }
}

// ---------- R-Tree ----------

TEST(RTreeTest, InsertQuery) {
    RTree r;
    r.insert(AABB2(Point2(0, 0), Point2(2, 2)), 1);
    r.insert(AABB2(Point2(3, 3), Point2(5, 5)), 2);
    r.insert(AABB2(Point2(1, 4), Point2(4, 6)), 3);
    EXPECT_TRUE(r.size() == 3);
    EXPECT_TRUE(r.contains(1));

    // 点查询：点 (1,1) 只在矩形 1 内
    auto pp = r.pointQuery(Point2(1, 1));
    EXPECT_TRUE(pp.size() == 1);
    EXPECT_TRUE(pp[0] == 1);

    // 点 (3.5,4.5) 在矩形 2 和 3 内
    auto pp2 = r.pointQuery(Point2(3.5, 4.5));
    std::set<int> s2(pp2.begin(), pp2.end());
    EXPECT_TRUE(s2.count(2) == 1);
    EXPECT_TRUE(s2.count(3) == 1);

    // 范围查询：(2,2)-(4,4) 与矩形 1(角)、2、3 相交
    auto rq = r.rangeQuery(AABB2(Point2(2, 2), Point2(4, 4)));
    EXPECT_TRUE(rq.size() >= 2);  // 至少 2,3
}

TEST(RTreeTest, Remove) {
    RTree r;
    for (int i = 1; i <= 5; ++i) {
        r.insert(AABB2(Point2(i, i), Point2(i + 1, i + 1)), i);
    }
    EXPECT_TRUE(r.size() == 5);
    EXPECT_TRUE(r.remove(3));
    EXPECT_TRUE(!r.contains(3));
    EXPECT_TRUE(r.size() == 4);
    // 删除后查询仍正确
    auto all = r.allData();
    EXPECT_TRUE(all.size() == 4);
    std::set<int> s(all.begin(), all.end());
    EXPECT_TRUE(s.count(3) == 0);
}

TEST(RTreeTest, Split) {
    // 插入超过 maxEntries 触发分裂，查询仍应全部命中
    RTree r(4, 2);
    for (int i = 0; i < 20; ++i) {
        r.insert(AABB2(Point2(i, i), Point2(i + 1, i + 1)), i);
    }
    EXPECT_TRUE(r.size() == 20);
    // 每个 ID 都能查到
    for (int i = 0; i < 20; ++i) {
        auto hit = r.pointQuery(Point2(i + 0.5, i + 0.5));
        EXPECT_TRUE(hit.size() == 1);
        EXPECT_TRUE(hit[0] == i);
    }
}

TEST(BSPTreeTest, Basic) {
    BSPTree bs(AABB2(Point2(0, 0), Point2(10, 10)), 3);
    auto pts = samplePoints();
    for (const auto& p : pts)
        EXPECT_TRUE(bs.insert(p));
    EXPECT_TRUE(bs.size() == 10);
    EXPECT_TRUE(bs.contains(Point2(5, 4)));
    EXPECT_TRUE(!bs.contains(Point2(100, 100)));
    // 越界拒绝
    EXPECT_TRUE(!bs.insert(Point2(-1, -1)));

    // NN
    Point2 nearest;
    EXPECT_TRUE(bs.nearestNeighbor(Point2(4.9, 4.1), nearest));
    EXPECT_NEAR(nearest.x, 5.0, 1e-9);

    // KNN
    auto knn = bs.kNearestNeighbors(Point2(5, 5), 3);
    EXPECT_TRUE(knn.size() == 3);

    // Range
    auto in = bs.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    EXPECT_TRUE(in.size() == 4);

    // Remove
    EXPECT_TRUE(bs.remove(Point2(5, 4)));
    EXPECT_TRUE(bs.size() == 9);
    EXPECT_TRUE(!bs.contains(Point2(5, 4)));
}

TEST(BVHTreeTest, Basic) {
    BVHTree bv(AABB2(Point2(0, 0), Point2(10, 10)), 3);
    bv.build(samplePoints());
    EXPECT_TRUE(bv.size() == 10);
    EXPECT_TRUE(bv.contains(Point2(5, 4)));
    EXPECT_TRUE(!bv.contains(Point2(100, 100)));

    Point2 nearest;
    EXPECT_TRUE(bv.nearestNeighbor(Point2(4.9, 4.1), nearest));
    EXPECT_NEAR(nearest.x, 5.0, 1e-9);

    auto knn = bv.kNearestNeighbors(Point2(5, 5), 3);
    EXPECT_TRUE(knn.size() == 3);

    auto in = bv.rangeQuery(AABB2(Point2(0, 0), Point2(6, 6)));
    EXPECT_TRUE(in.size() == 4);

    // Insert incrementally
    EXPECT_TRUE(bv.insert(Point2(7, 8)));
    EXPECT_TRUE(bv.size() == 11);

    // Remove
    EXPECT_TRUE(bv.remove(Point2(5, 4)));
    EXPECT_TRUE(bv.size() == 10);
    EXPECT_TRUE(!bv.contains(Point2(5, 4)));
}
