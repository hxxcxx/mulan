/**
 * @file primitive_pick_index_tests.cpp
 * @brief 验证资产级图元 BVH 与原线性拾取严格等价，并覆盖缓存失效与保守回退。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include "scene_sync/detail/asset_picking.h"
#include "scene_sync/detail/primitive_pick_index.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/scene/scene.h>
#include <mulan/view/scene_sync/render_scene.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace mulan::view::detail {
namespace {

graphics::Mesh makePositionMesh(graphics::PrimitiveTopology topology, const std::vector<math::Point3>& positions) {
    graphics::Mesh mesh;
    mesh.layout.begin().add(graphics::VertexSemantic::Position, graphics::VertexFormat::Float3).end();
    mesh.topology = topology;
    mesh.vertices.resize(positions.size() * mesh.layout.stride());
    for (size_t index = 0; index < positions.size(); ++index) {
        const float value[3]{ static_cast<float>(positions[index].x), static_cast<float>(positions[index].y),
                              static_cast<float>(positions[index].z) };
        std::memcpy(mesh.vertices.data() + index * mesh.layout.stride(), value, sizeof(value));
    }
    mesh.computeBounds();
    return mesh;
}

graphics::Mesh makeSeparatedTriangles(size_t count, double spacing = 3.0) {
    std::vector<math::Point3> positions;
    positions.reserve(count * 3u);
    for (size_t index = 0; index < count; ++index) {
        const double x = static_cast<double>(index) * spacing;
        positions.emplace_back(x - 0.4, -0.4, 0.0);
        positions.emplace_back(x + 0.4, -0.4, 0.0);
        positions.emplace_back(x, 0.4, 0.0);
    }
    return makePositionMesh(graphics::PrimitiveTopology::TriangleList, positions);
}

graphics::Mesh makeSeparatedLines(size_t count, double spacing = 2.0) {
    std::vector<math::Point3> positions;
    positions.reserve(count * 2u);
    for (size_t index = 0; index < count; ++index) {
        const double x = static_cast<double>(index) * spacing;
        positions.emplace_back(x, -0.5, 0.0);
        positions.emplace_back(x, 0.5, 0.0);
    }
    return makePositionMesh(graphics::PrimitiveTopology::LineList, positions);
}

void expectSameHits(const std::vector<MeshPickResult>& expected, const std::vector<MeshPickResult>& actual) {
    const auto expectSameScalar = [](double expectedValue, double actualValue) {
        if (std::isnan(expectedValue)) {
            EXPECT_TRUE(std::isnan(actualValue));
        } else {
            EXPECT_NEAR(actualValue, expectedValue, 1.0e-10);
        }
    };
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index].kind, expected[index].kind);
        EXPECT_EQ(actual[index].sourceDrawableIndex, expected[index].sourceDrawableIndex);
        EXPECT_EQ(actual[index].primitiveIndex, expected[index].primitiveIndex);
        ASSERT_EQ(actual[index].distance.has_value(), expected[index].distance.has_value());
        if (expected[index].distance) {
            expectSameScalar(*expected[index].distance, *actual[index].distance);
        }
        expectSameScalar(expected[index].worldPoint.x, actual[index].worldPoint.x);
        expectSameScalar(expected[index].worldPoint.y, actual[index].worldPoint.y);
        expectSameScalar(expected[index].worldPoint.z, actual[index].worldPoint.z);
    }
}

TEST(PrimitivePickIndexTests, TriangleCandidatesMatchLinearPathAndReduceExactTests) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("TriangleGrid");
    geometry->addPrimitive(makeSeparatedTriangles(512));
    const math::Ray3 ray(math::Point3(300.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));

    std::vector<MeshPickResult> expected;
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::identity(), 0.0, expected);

    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    PrimitivePickQueryStats stats;
    std::vector<MeshPickResult> actual;
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::identity(), 0.0, actual, &cache, &stats);

    expectSameHits(expected, actual);
    ASSERT_EQ(actual.size(), 1u);
    EXPECT_EQ(actual.front().primitiveIndex, 100u);
    EXPECT_EQ(stats.indexedPrimitiveCount, 512u);
    EXPECT_LT(stats.candidatePrimitiveCount, 16u);
    EXPECT_LT(stats.candidatePrimitiveCount, stats.indexedPrimitiveCount);
    std::vector<asset::Drawable> drawables;
    geometry->collectDrawables(drawables);
    const PrimitivePickIndex* index = cache.get(*geometry, drawables);
    ASSERT_NE(index, nullptr);
    EXPECT_LE(index->nodeCount(), 2u * ((index->primitiveCount() + 3u) / 4u) - 1u);
}

TEST(PrimitivePickIndexTests, LineToleranceRemainsConservativeUnderNonUniformScale) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("ScaledLines");
    geometry->addPrimitive(makeSeparatedLines(256));
    const math::Mat4 transform =
            math::Mat4::translate(math::Vec3(7.0, -3.0, 2.0)) * math::Mat4::scale(math::Vec3(4.0, 0.25, 3.0));
    const double localX = 84.0;
    const math::Ray3 ray(math::Point3(7.0 + localX * 4.0 + 0.04, -3.0, 10.0), math::Vec3(0.0, 0.0, -1.0));

    std::vector<MeshPickResult> expected;
    appendGeometryAssetPickCandidates(ray, *geometry, transform, 0.05, expected);

    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    PrimitivePickQueryStats stats;
    std::vector<MeshPickResult> actual;
    appendGeometryAssetPickCandidates(ray, *geometry, transform, 0.05, actual, &cache, &stats);

    expectSameHits(expected, actual);
    ASSERT_EQ(actual.size(), 1u);
    EXPECT_EQ(actual.front().primitiveIndex, 42u);
    EXPECT_LT(stats.candidatePrimitiveCount, stats.indexedPrimitiveCount);
}

TEST(PrimitivePickIndexTests, TinyNonZeroRayDirectionCannotBeCulledBeforeFarExactHit) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("FarTriangle");
    geometry->addPrimitive(makePositionMesh(
            graphics::PrimitiveTopology::TriangleList,
            { math::Point3(0.0, -1.0, -1.0e16), math::Point3(2.0, -1.0, -1.0e16), math::Point3(1.0, 1.0, -1.0e16) }));
    const math::Ray3 ray(math::Point3::origin(), math::Vec3(1.0e-16, 0.0, -1.0));

    const MeshPickResult expected = pickGeometryAsset(ray, *geometry, math::Mat4::identity(), 0.0);
    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    PrimitivePickQueryStats stats;
    const MeshPickResult actual = pickGeometryAsset(ray, *geometry, math::Mat4::identity(), 0.0, &cache, &stats);

    ASSERT_TRUE(expected.distance);
    ASSERT_TRUE(actual.distance);
    EXPECT_DOUBLE_EQ(*actual.distance, *expected.distance);
    EXPECT_EQ(actual.primitiveIndex, expected.primitiveIndex);
    EXPECT_TRUE(stats.usedIndex);
    EXPECT_FALSE(stats.linearFallback);
}

TEST(PrimitivePickIndexTests, CacheReusesAssetRevisionAcrossTransformChangesAndRebuildsOnContentChange) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("VersionedMesh");
    geometry->addPrimitive(makeSeparatedTriangles(32));
    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    const math::Ray3 ray(math::Point3(0.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));

    std::vector<MeshPickResult> hits;
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::identity(), 0.0, hits, &cache);
    ASSERT_EQ(cache.buildCount(), 1u);
    hits.clear();
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::translate(math::Vec3(5.0, 0.0, 0.0)), 0.0, hits,
                                      &cache);
    EXPECT_EQ(cache.buildCount(), 1u);

    geometry->addPrimitive(makeSeparatedLines(4));
    hits.clear();
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::identity(), 0.1, hits, &cache);
    EXPECT_EQ(cache.buildCount(), 2u);
    EXPECT_EQ(cache.entryCount(), 1u);

    cache.erase(geometry->id());
    EXPECT_EQ(cache.entryCount(), 0u);
    hits.clear();
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::identity(), 0.1, hits, &cache);
    EXPECT_EQ(cache.buildCount(), 3u);
}

TEST(PrimitivePickIndexTests, InvalidVerticesRejectPartialIndexAndPreserveLinearFallback) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("InvalidMesh");
    graphics::Mesh mesh = makeSeparatedTriangles(2);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(mesh.vertices.data() + mesh.layout.stride() * 3u, &nan, sizeof(nan));
    geometry->addPrimitive(std::move(mesh));
    const math::Ray3 ray(math::Point3(0.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));

    std::vector<MeshPickResult> expected;
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::identity(), 0.0, expected);
    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    std::vector<MeshPickResult> actual;
    appendGeometryAssetPickCandidates(ray, *geometry, math::Mat4::identity(), 0.0, actual, &cache);

    expectSameHits(expected, actual);
    EXPECT_EQ(cache.buildCount(), 1u);
}

TEST(PrimitivePickIndexTests, SingularTransformSkipsIndexAndPreservesLegacyResult) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("SingularMesh");
    geometry->addPrimitive(makeSeparatedTriangles(2));
    const math::Mat4 singular = math::Mat4::scale(math::Vec3(1.0, 0.0, 1.0));
    const math::Ray3 ray(math::Point3(0.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));

    const MeshPickResult expected = pickGeometryAsset(ray, *geometry, singular, 0.0);
    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    const MeshPickResult actual = pickGeometryAsset(ray, *geometry, singular, 0.0, &cache);

    EXPECT_EQ(actual.tested, expected.tested);
    EXPECT_EQ(actual.distance.has_value(), expected.distance.has_value());
    if (expected.distance) {
        EXPECT_NEAR(*actual.distance, *expected.distance, 1.0e-10);
        EXPECT_EQ(actual.primitiveIndex, expected.primitiveIndex);
    }
    EXPECT_EQ(cache.buildCount(), 0u);
}

TEST(PrimitivePickIndexTests, LargeTranslationCannotHideProjectiveTransformFromFallback) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("ProjectiveMesh");
    geometry->addPrimitive(makeSeparatedTriangles(2));
    math::Mat4 projective = math::Mat4::translate(math::Vec3(1.0e15, 0.0, 0.0));
    projective[0].w = 1.0e-3;
    const math::Ray3 ray(math::Point3(0.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));

    const MeshPickResult expected = pickGeometryAsset(ray, *geometry, projective, 0.0);
    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    const MeshPickResult actual = pickGeometryAsset(ray, *geometry, projective, 0.0, &cache);

    EXPECT_EQ(actual.tested, expected.tested);
    EXPECT_EQ(actual.distance.has_value(), expected.distance.has_value());
    if (expected.distance && std::isfinite(*expected.distance)) {
        EXPECT_NEAR(*actual.distance, *expected.distance, 1.0e-10);
    }
    EXPECT_EQ(cache.buildCount(), 0u);
}

TEST(PrimitivePickIndexTests, TinyProjectiveTermWithHugeTranslationCannotMissLine) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("AlmostAffineLine");
    geometry->addPrimitive(makePositionMesh(graphics::PrimitiveTopology::LineList,
                                            { math::Point3(0.0, -1.0, 0.0), math::Point3(0.0, 1.0, 0.0) }));
    math::Mat4 transform = math::Mat4::translate(math::Vec3(1.0e15, 0.0, 0.0));
    transform[0].w = 1.0e-15;
    const math::Ray3 ray(math::Point3(1.0e15, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));

    const MeshPickResult expected = pickGeometryAsset(ray, *geometry, transform, 0.01);
    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    PrimitivePickQueryStats stats;
    const MeshPickResult actual = pickGeometryAsset(ray, *geometry, transform, 0.01, &cache, &stats);

    ASSERT_TRUE(expected.distance);
    ASSERT_TRUE(actual.distance);
    EXPECT_DOUBLE_EQ(*actual.distance, *expected.distance);
    EXPECT_EQ(actual.primitiveIndex, expected.primitiveIndex);
    EXPECT_EQ(cache.buildCount(), 0u);
    EXPECT_TRUE(stats.linearFallback);
    EXPECT_EQ(stats.candidatePrimitiveCount, 1u);
}

TEST(PrimitivePickIndexTests, OverlappingPrimitiveTieKeepsStableLegacyOrder) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("OverlappingMesh");
    graphics::Mesh mesh = makeSeparatedTriangles(1);
    const size_t originalBytes = mesh.vertices.size();
    mesh.vertices.resize(originalBytes * 2u);
    std::memcpy(mesh.vertices.data() + originalBytes, mesh.vertices.data(), originalBytes);
    mesh.computeBounds();
    geometry->addPrimitive(std::move(mesh));
    const math::Ray3 ray(math::Point3(0.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));

    const MeshPickResult expected = pickGeometryAsset(ray, *geometry, math::Mat4::identity(), 0.0);
    PrimitivePickIndexCache cache;
    cache.bindDomain(assets.domainId());
    const MeshPickResult actual = pickGeometryAsset(ray, *geometry, math::Mat4::identity(), 0.0, &cache);

    ASSERT_TRUE(expected.distance);
    ASSERT_TRUE(actual.distance);
    EXPECT_EQ(expected.primitiveIndex, 0u);
    EXPECT_EQ(actual.primitiveIndex, expected.primitiveIndex);
    EXPECT_DOUBLE_EQ(*actual.distance, *expected.distance);
}

TEST(PrimitivePickIndexTests, RenderSceneOwnsVersionedCacheAcrossTransformAndAssetChanges) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("SceneIndexedMesh");
    geometry->addPrimitive(makeSeparatedTriangles(512));
    scene::Scene source;
    const scene::EntityId entity = source.createEntity("IndexedEntity");
    ASSERT_TRUE(source.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(source, assets);
    PickQueryStats firstStats;
    const math::Ray3 firstRay(math::Point3(300.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));
    const auto first = renderScene.pick(firstRay, 0.0, &firstStats);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->primitiveIndex, 100u);
    EXPECT_EQ(firstStats.primitiveIndexBuildCount, 1u);
    EXPECT_EQ(firstStats.indexedPrimitiveCount, 512u);
    EXPECT_LT(firstStats.candidatePrimitiveCount, firstStats.indexedPrimitiveCount);

    PickQueryStats reusedStats;
    ASSERT_TRUE(renderScene.pick(firstRay, 0.0, &reusedStats));
    EXPECT_EQ(reusedStats.primitiveIndexBuildCount, 0u);

    ASSERT_TRUE(source.setWorldTransform(entity, math::Mat4::translate(math::Vec3(10.0, 0.0, 0.0))));
    renderScene.sync(source, assets);
    PickQueryStats transformedStats;
    const math::Ray3 transformedRay(math::Point3(310.0, 0.0, 5.0), math::Vec3(0.0, 0.0, -1.0));
    ASSERT_TRUE(renderScene.pick(transformedRay, 0.0, &transformedStats));
    EXPECT_EQ(transformedStats.primitiveIndexBuildCount, 0u);

    geometry->addPrimitive(makeSeparatedLines(4));
    renderScene.sync(source, assets);
    PickQueryStats changedStats;
    ASSERT_TRUE(renderScene.pick(transformedRay, 0.01, &changedStats));
    EXPECT_EQ(changedStats.primitiveIndexBuildCount, 1u);
    EXPECT_EQ(changedStats.indexedPrimitiveCount, 516u);
}

}  // namespace
}  // namespace mulan::view::detail
