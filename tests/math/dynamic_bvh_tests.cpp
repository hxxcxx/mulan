/**
 * @file dynamic_bvh_tests.cpp
 * @brief 通用三维动态 BVH 的结构、查询与增量变更测试。
 * @author hxxcxx
 * @date 2026-07-16
 */
#include <mulan/math/spatial/dynamic_bvh.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <unordered_map>
#include <vector>

namespace mulan::math::tests {
namespace {

struct TestId {
    TestId() = delete;
    explicit TestId(int value_) : value(value_) {}

    int value;
    friend bool operator==(const TestId&, const TestId&) = default;
};

struct TestIdHash {
    size_t operator()(const TestId& id) const noexcept { return std::hash<int>{}(id.value); }
};

using TestTree = DynamicBVH<TestId, TestIdHash>;

AABB3 boxAt(double x, double y = 0.0, double z = 0.0, double halfExtent = 0.5) {
    return AABB3(Point3(x - halfExtent, y - halfExtent, z - halfExtent),
                 Point3(x + halfExtent, y + halfExtent, z + halfExtent));
}

AABB3 expandedForQuery(const AABB3& bounds, double padding) {
    if (padding <= 0.0)
        return bounds;
    return AABB3(bounds.min - Vec3(padding), bounds.max + Vec3(padding));
}

template <typename Tree>
std::set<int> queryBounds(const Tree& tree, const AABB3& bounds) {
    std::set<int> result;
    EXPECT_TRUE(tree.queryBounds(bounds, [&result](const TestId& id) {
        result.insert(id.value);
        return true;
    }));
    return result;
}

template <typename Tree>
std::set<int> queryFrustum(const Tree& tree, const Frustum3& frustum, double padding = 0.0) {
    std::set<int> result;
    EXPECT_TRUE(tree.queryFrustum(frustum, padding, [&result](const TestId& id) {
        result.insert(id.value);
        return true;
    }));
    return result;
}

TEST(FrustumTests, TryFromViewProjectionRejectsNonFiniteAndDegenerateMatrices) {
    const std::optional<Frustum3> identity = Frustum3::tryFromViewProjection(Mat4::identity());
    ASSERT_TRUE(identity.has_value());
    EXPECT_TRUE(identity->contains(Point3(0.0, 0.0, 0.0)));
    EXPECT_FALSE(identity->contains(Point3(2.0, 0.0, 0.0)));

    EXPECT_FALSE(Frustum3::tryFromViewProjection(Mat4(0.0)).has_value());

    Mat4 singular = Mat4::identity();
    singular[3][3] = 0.0;
    EXPECT_FALSE(Frustum3::tryFromViewProjection(singular).has_value());

    // 最后一行严格等于 -10 * row0 + 6 * row1。普通 double 消元会留下约
    // 2.6e-18 的伪主元；该残差不能被误认为有效视锥，否则会错误裁掉原点。
    const double dependentRows[4][4] = {
        { 10.0, 6.0, 4.0, 3.0 }, { -8.0, -8.0, 10.0, -3.0 }, { 6.0, -2.0, -9.0, 4.0 }, { -148.0, -108.0, 20.0, -48.0 }
    };
    Mat4 numericallySingular(0.0);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column)
            numericallySingular[column][row] = dependentRows[row][column];
    }
    EXPECT_FALSE(Frustum3::tryFromViewProjection(numericallySingular).has_value());

    Mat4 notANumber = Mat4::identity();
    notANumber[1][2] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(Frustum3::tryFromViewProjection(notANumber).has_value());

    Mat4 infinite = Mat4::identity();
    infinite[2][0] = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(Frustum3::tryFromViewProjection(infinite).has_value());

    // 大世界平移会让矩阵条件数很差，但不等于矩阵退化，不能因此关闭有效视锥。
    const Mat4 largeWorldTranslation = Mat4::translate(Vec3(1.0e200, -2.0e200, 3.0e200));
    EXPECT_TRUE(Frustum3::tryFromViewProjection(largeWorldTranslation).has_value());

    // 兼容入口对非法矩阵采用保守 fallback，不因相机瞬时无效而误裁整个场景。
    const Frustum3 fallback = Frustum3::fromViewProjection(Mat4(0.0));
    EXPECT_TRUE(fallback.intersects(boxAt(1.0e6, -1.0e6, 1.0e6)));
}

TEST(FrustumTests, LargeWorldBoundaryUsesAConservativeRoundoffBound) {
    constexpr double origin = 1.0e12;
    const Mat4 viewProjection = Mat4::translate(Vec3(-origin, origin, -origin));
    const std::optional<Frustum3> frustum = Frustum3::tryFromViewProjection(viewProjection);
    ASSERT_TRUE(frustum.has_value());

    // x = origin + 1 正好位于右裁剪面。平面距离在这里是约 1e12 的两个数相减，
    // 固定世界容差不足以覆盖舍入误差，但边界对象必须保守保留。
    const AABB3 boundary(Point3(origin + 1.0, -origin, origin), Point3(origin + 1.0, -origin, origin));
    EXPECT_TRUE(frustum->intersects(boundary));

    const AABB3 clearlyOutside(Point3(origin + 2.0, -origin, origin), Point3(origin + 3.0, -origin, origin));
    EXPECT_FALSE(frustum->intersects(clearlyOutside));
}

TEST(DynamicBVHTests, FrustumQueryReturnsFixedExpectedSetAndStatistics) {
    const std::optional<Frustum3> frustum = Frustum3::tryFromViewProjection(Mat4::identity());
    ASSERT_TRUE(frustum.has_value());

    TestTree tree;
    ASSERT_TRUE(tree.insert(TestId(0), boxAt(0.0, 0.0, 0.0, 0.25)));
    ASSERT_TRUE(tree.insert(TestId(1), boxAt(1.2, 0.0, 0.0, 0.25)));
    ASSERT_TRUE(tree.insert(TestId(2), boxAt(1.5, 0.0, 0.0, 0.25)));
    ASSERT_TRUE(tree.insert(TestId(3), boxAt(0.0, 0.0, -2.0, 0.25)));
    ASSERT_TRUE(tree.validate());

    TestTree::QueryStats stats;
    std::set<int> actual;
    EXPECT_TRUE(tree.queryFrustum(
            *frustum, 0.0,
            [&actual](const TestId& id) {
                actual.insert(id.value);
                return true;
            },
            &stats));
    EXPECT_EQ(actual, (std::set<int>{ 0, 1 }));
    EXPECT_EQ(stats.resultCount, actual.size());
    EXPECT_EQ(stats.leafBoundsTestCount, actual.size());
    EXPECT_GE(stats.nodeBoundsTestCount, stats.leafBoundsTestCount);
}

TEST(DynamicBVHTests, FrustumQueryMatchesRandomizedLinearReference) {
    TestTree tree;
    std::unordered_map<int, AABB3> reference;
    constexpr int kObjectCount = 512;
    tree.reserve(kObjectCount);

    std::mt19937 random(0x8b17u);
    std::uniform_real_distribution<double> centerDistribution(-100.0, 100.0);
    std::uniform_real_distribution<double> objectExtentDistribution(0.05, 3.0);
    for (int id = 0; id < kObjectCount; ++id) {
        const Point3 center(centerDistribution(random), centerDistribution(random), centerDistribution(random));
        const Vec3 extents(objectExtentDistribution(random), objectExtentDistribution(random),
                           objectExtentDistribution(random));
        const AABB3 bounds = AABB3::fromCenterExtents(center, extents);
        ASSERT_TRUE(tree.insert(TestId(id), bounds));
        reference.emplace(id, bounds);
    }
    ASSERT_TRUE(tree.validate());

    std::uniform_real_distribution<double> frustumCenterDistribution(-70.0, 70.0);
    std::uniform_real_distribution<double> frustumExtentDistribution(4.0, 35.0);
    std::uniform_real_distribution<double> angleDistribution(-3.0, 3.0);
    std::uniform_real_distribution<double> paddingDistribution(0.0, 2.0);
    for (int queryIndex = 0; queryIndex < 96; ++queryIndex) {
        const Vec3 center(frustumCenterDistribution(random), frustumCenterDistribution(random),
                          frustumCenterDistribution(random));
        const Vec3 extents(frustumExtentDistribution(random), frustumExtentDistribution(random),
                           frustumExtentDistribution(random));
        const Mat4 viewProjection = Mat4::scale(Vec3(1.0 / extents.x, 1.0 / extents.y, 1.0 / extents.z)) *
                                    Mat4::rotationY(angleDistribution(random)) *
                                    Mat4::rotationZ(angleDistribution(random)) * Mat4::translate(-center);
        const std::optional<Frustum3> frustum = Frustum3::tryFromViewProjection(viewProjection);
        ASSERT_TRUE(frustum.has_value()) << "query=" << queryIndex;
        const double padding = queryIndex % 4 == 0 ? 0.0 : paddingDistribution(random);

        std::set<int> expected;
        for (const auto& [id, bounds] : reference) {
            if (frustum->intersects(expandedForQuery(bounds, padding)))
                expected.insert(id);
        }
        EXPECT_EQ(queryFrustum(tree, *frustum, padding), expected) << "query=" << queryIndex;
    }
}

TEST(DynamicBVHTests, FrustumQueryAppliesPaddingAndNormalizesInvalidPadding) {
    const std::optional<Frustum3> frustum = Frustum3::tryFromViewProjection(Mat4::identity());
    ASSERT_TRUE(frustum.has_value());

    TestTree tree;
    ASSERT_TRUE(tree.insert(TestId(7), boxAt(1.2, 0.0, 0.0, 0.1)));
    ASSERT_TRUE(tree.insert(TestId(8), boxAt(3.0, 0.0, 0.0, 0.1)));
    ASSERT_GT(tree.height(), 0);
    EXPECT_TRUE(queryFrustum(tree, *frustum, 0.0).empty());
    EXPECT_EQ(queryFrustum(tree, *frustum, 0.11), std::set<int>{ 7 });
    EXPECT_TRUE(queryFrustum(tree, *frustum, -1.0).empty());
    EXPECT_TRUE(queryFrustum(tree, *frustum, std::numeric_limits<double>::quiet_NaN()).empty());
}

TEST(DynamicBVHTests, FrustumQuerySupportsEarlyCancellationWithCompleteStatistics) {
    const std::optional<Frustum3> frustum = Frustum3::tryFromViewProjection(Mat4::identity());
    ASSERT_TRUE(frustum.has_value());

    TestTree tree;
    for (int id = 0; id < 64; ++id) {
        const double x = -0.7 + static_cast<double>(id % 8) * 0.2;
        const double y = -0.7 + static_cast<double>(id / 8) * 0.2;
        ASSERT_TRUE(tree.insert(TestId(id), boxAt(x, y, 0.0, 0.01)));
    }

    TestTree::QueryStats stats;
    size_t visitCount = 0;
    EXPECT_FALSE(tree.queryFrustum(
            *frustum, 0.0,
            [&visitCount](const TestId&) {
                ++visitCount;
                return false;
            },
            &stats));
    EXPECT_EQ(visitCount, 1u);
    EXPECT_EQ(stats.resultCount, 1u);
    EXPECT_EQ(stats.leafBoundsTestCount, 1u);
    EXPECT_GE(stats.nodeBoundsTestCount, 1u);
}

TEST(DynamicBVHTests, SupportsNonDefaultConstructibleIdsAndRejectsInvalidInput) {
    TestTree tree;
    tree.reserve(4);

    EXPECT_TRUE(tree.insert(TestId(7), boxAt(2.0)));
    EXPECT_FALSE(tree.insert(TestId(7), boxAt(5.0)));
    EXPECT_FALSE(tree.insert(TestId(8), AABB3::empty()));

    AABB3 nonFinite = boxAt(0.0);
    nonFinite.max.x = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(tree.insert(TestId(9), nonFinite));
    EXPECT_EQ(tree.size(), 1u);
    EXPECT_TRUE(tree.contains(TestId(7)));
    ASSERT_NE(tree.boundsOf(TestId(7)), nullptr);
    EXPECT_DOUBLE_EQ(tree.boundsOf(TestId(7))->min.x, 1.5);
    EXPECT_EQ(tree.update(TestId(99), boxAt(1.0)), TestTree::UpdateResult::Rejected);
    EXPECT_EQ(tree.update(TestId(7), boxAt(2.0)), TestTree::UpdateResult::Unchanged);
    EXPECT_TRUE(tree.validate());
}

TEST(DynamicBVHTests, UpdateAndRemoveMaintainBoundsAndHierarchy) {
    TestTree tree;
    for (int i = 0; i < 96; ++i) {
        ASSERT_TRUE(tree.insert(TestId(i), boxAt(static_cast<double>(i) * 3.0)));
        ASSERT_TRUE(tree.validate());
    }

    EXPECT_EQ(tree.update(TestId(17), boxAt(-50.0, 4.0)), TestTree::UpdateResult::Updated);
    EXPECT_FALSE(queryBounds(tree, boxAt(51.0, 0.0, 0.0, 0.75)).contains(17));
    EXPECT_TRUE(queryBounds(tree, boxAt(-50.0, 4.0, 0.0, 0.75)).contains(17));
    EXPECT_TRUE(tree.validate());

    for (int i = 0; i < 96; i += 2) {
        EXPECT_TRUE(tree.remove(TestId(i)));
        ASSERT_TRUE(tree.validate());
    }
    EXPECT_FALSE(tree.remove(TestId(0)));
    EXPECT_EQ(tree.size(), 48u);
    EXPECT_GE(tree.height(), 1);
}

TEST(DynamicBVHTests, IncrementalQueriesMatchLinearReference) {
    TestTree tree;
    std::unordered_map<int, AABB3> reference;
    tree.reserve(320);
    for (int i = 0; i < 320; ++i) {
        const double x = static_cast<double>((i * 37) % 101) - 50.0;
        const double y = static_cast<double>((i * 19) % 67) - 33.0;
        const double z = static_cast<double>((i * 11) % 29) - 14.0;
        const AABB3 bounds = boxAt(x, y, z, 0.25 + static_cast<double>(i % 5) * 0.1);
        ASSERT_TRUE(tree.insert(TestId(i), bounds));
        reference.emplace(i, bounds);
    }

    for (int i = 0; i < 320; i += 4) {
        const AABB3 moved = boxAt(-120.0 + i * 0.5, 10.0 - i * 0.02, 3.0, 0.4);
        ASSERT_EQ(tree.update(TestId(i), moved), TestTree::UpdateResult::Updated);
        reference.at(i) = moved;
    }
    for (int i = 3; i < 320; i += 7) {
        ASSERT_TRUE(tree.remove(TestId(i)));
        reference.erase(i);
    }
    ASSERT_TRUE(tree.validate());

    for (int q = 0; q < 80; ++q) {
        const double x = -130.0 + q * 3.5;
        const double y = -35.0 + static_cast<double>((q * 13) % 70);
        const AABB3 query = boxAt(x, y, 0.0, 6.0 + static_cast<double>(q % 3));
        std::set<int> expected;
        for (const auto& [id, bounds] : reference) {
            if (bounds.intersects(query))
                expected.insert(id);
        }
        EXPECT_EQ(queryBounds(tree, query), expected) << "query=" << q;
    }
}

TEST(DynamicBVHTests, DeterministicMutationStressKeepsEveryInvariant) {
    TestTree tree;
    std::unordered_map<int, AABB3> reference;
    std::mt19937 random(0x5a17u);
    for (int step = 0; step < 3000; ++step) {
        const int id = static_cast<int>(random() % 192u);
        const unsigned operation = random() % 5u;
        const auto known = reference.find(id);
        if (known == reference.end()) {
            const AABB3 bounds = boxAt(static_cast<double>(static_cast<int>(random() % 401u) - 200),
                                       static_cast<double>(static_cast<int>(random() % 151u) - 75),
                                       static_cast<double>(static_cast<int>(random() % 81u) - 40),
                                       0.1 + static_cast<double>(random() % 20u) * 0.05);
            ASSERT_TRUE(tree.insert(TestId(id), bounds));
            reference.emplace(id, bounds);
        } else if (operation == 0u) {
            ASSERT_TRUE(tree.remove(TestId(id)));
            reference.erase(known);
        } else {
            const AABB3 bounds = boxAt(static_cast<double>(static_cast<int>(random() % 401u) - 200),
                                       static_cast<double>(static_cast<int>(random() % 151u) - 75),
                                       static_cast<double>(static_cast<int>(random() % 81u) - 40),
                                       0.1 + static_cast<double>(random() % 20u) * 0.05);
            ASSERT_EQ(tree.update(TestId(id), bounds), TestTree::UpdateResult::Updated);
            known->second = bounds;
        }
        ASSERT_TRUE(tree.validate()) << "step=" << step;

        if (step % 37 == 0) {
            const AABB3 query = boxAt(static_cast<double>(static_cast<int>(random() % 401u) - 200),
                                      static_cast<double>(static_cast<int>(random() % 151u) - 75), 0.0, 12.0);
            std::set<int> expected;
            for (const auto& [candidate, bounds] : reference) {
                if (bounds.intersects(query))
                    expected.insert(candidate);
            }
            EXPECT_EQ(queryBounds(tree, query), expected) << "step=" << step;
        }
    }
}

TEST(DynamicBVHTests, RayQueryPrunesAndSupportsEarlyCancellation) {
    TestTree tree;
    for (int i = 0; i < 256; ++i)
        ASSERT_TRUE(tree.insert(TestId(i), boxAt(i * 4.0, i == 73 ? 0.0 : 20.0)));
    ASSERT_TRUE(tree.validate());

    TestTree::QueryStats stats;
    std::vector<int> hits;
    EXPECT_TRUE(tree.queryRay(
            Ray3(Point3(-10.0, 0.0, 0.0), Vec3(1.0, 0.0, 0.0)), 0.0,
            [&hits](const TestId& id) {
                hits.push_back(id.value);
                return true;
            },
            &stats));
    EXPECT_EQ(hits, std::vector<int>{ 73 });
    EXPECT_EQ(stats.resultCount, 1u);
    EXPECT_LT(stats.nodeBoundsTestCount, tree.size());

    size_t visits = 0;
    EXPECT_FALSE(tree.queryBounds(tree.bounds(), [&visits](const TestId&) {
        ++visits;
        return false;
    }));
    EXPECT_EQ(visits, 1u);
}

TEST(DynamicBVHTests, ClearPreservesReusableCapacityWithoutStaleNodes) {
    TestTree tree;
    for (int i = 0; i < 32; ++i)
        ASSERT_TRUE(tree.insert(TestId(i), boxAt(i)));
    tree.clear();
    EXPECT_TRUE(tree.empty());
    EXPECT_TRUE(tree.bounds().isEmpty());
    EXPECT_TRUE(tree.validate());

    EXPECT_TRUE(tree.insert(TestId(100), boxAt(8.0)));
    EXPECT_TRUE(tree.validate());
    EXPECT_EQ(queryBounds(tree, boxAt(8.0)), std::set<int>{ 100 });
}

}  // namespace
}  // namespace mulan::math::tests
