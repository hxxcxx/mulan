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

template <typename Tree>
std::set<int> queryBounds(const Tree& tree, const AABB3& bounds) {
    std::set<int> result;
    EXPECT_TRUE(tree.queryBounds(bounds, [&result](const TestId& id) {
        result.insert(id.value);
        return true;
    }));
    return result;
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
