/**
 * @file render_scene_spatial_index_tests.cpp
 * @brief RenderScene 场景级 BVH、保守 fallback 与拾取统计的确定性测试。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "scene_sync/detail/asset_picking.h"
#include "scene_sync/detail/picking_types.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/math/algo/intersect.h>
#include <mulan/scene/scene.h>
#include <mulan/view/scene_sync/render_scene.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mulan::view {
namespace {

class EmptyBoundsCurveAsset final : public asset::CurveAsset {
public:
    EmptyBoundsCurveAsset(asset::AssetId id, std::string name) : CurveAsset(id, std::move(name)) {}

    math::AABB3 localBounds() const override { return math::AABB3::empty(); }
};

asset::CurveAsset* makeLineAsset(asset::AssetLibrary& assets, double halfLength = 0.4) {
    auto* curve = assets.create<asset::CurveAsset>("PickLine");
    curve->addSegment(math::Segment3(math::Point3(-halfLength, 0.0, 0.0), math::Point3(halfLength, 0.0, 0.0)));
    return curve;
}

scene::EntityId addCurveEntity(scene::Scene& scene, asset::AssetId geometry, double x, double y = 0.0) {
    const scene::EntityId entity = scene.createEntity("CurveEntity");
    EXPECT_TRUE(scene.setGeometry(entity, geometry));
    EXPECT_TRUE(scene.setWorldTransform(entity, math::Mat4::translate(math::Vec3(x, y, 0.0))));
    return entity;
}

math::Ray3 verticalRay(double x, double y = 0.0) {
    return math::Ray3(math::Point3(x, y, 5.0), math::Vec3(0.0, 0.0, -1.0));
}

std::optional<RenderScene::PickResult> linearPick(const RenderScene& scene, const asset::AssetLibrary& assets,
                                                  const math::Ray3& ray, double lineToleranceWorld) {
    std::optional<RenderScene::PickResult> best;
    scene.forEachProxy([&](const SceneProxy& proxy) {
        if (!proxy.visible) {
            return;
        }

        const bool emptyBounds = proxy.worldBounds.isEmpty();
        const math::Hit3 boundsHit =
                math::intersect(ray, detail::expandedBounds(proxy.worldBounds, lineToleranceWorld));
        if (!emptyBounds && !boundsHit.hit) {
            return;
        }

        std::optional<RenderScene::PickResult> candidate;
        if (!emptyBounds) {
            candidate = RenderScene::PickResult{
                .entity = proxy.entity,
                .pickId = proxy.pickId,
                .distance = boundsHit.t,
                .kind = RenderScene::PickHitKind::Object,
                .toleranceWorld = lineToleranceWorld,
            };
        }

        if (const asset::Asset* geometry = assets.asset(proxy.geometry)) {
            const detail::MeshPickResult meshHit =
                    detail::pickGeometryAsset(ray, *geometry, proxy.worldTransform, lineToleranceWorld);
            if (meshHit.tested) {
                if (!meshHit.distance) {
                    return;
                }
                candidate = detail::pickResultFromMeshHit(proxy.entity, proxy, meshHit, lineToleranceWorld);
            }
        }

        if (candidate && (!best || candidate->distance < best->distance)) {
            best = std::move(candidate);
        }
    });
    return best;
}

std::vector<RenderScene::PickResult> linearCollect(const RenderScene& scene, const asset::AssetLibrary& assets,
                                                   const math::Ray3& ray, double lineToleranceWorld) {
    std::vector<RenderScene::PickResult> results;
    std::vector<detail::MeshPickResult> meshHits;
    scene.forEachProxy([&](const SceneProxy& proxy) {
        if (!proxy.visible) {
            return;
        }
        if (!proxy.worldBounds.isEmpty() &&
            !math::intersect(ray, detail::expandedBounds(proxy.worldBounds, lineToleranceWorld)).hit) {
            return;
        }

        const asset::Asset* geometry = assets.asset(proxy.geometry);
        if (!geometry) {
            return;
        }
        meshHits.clear();
        detail::appendGeometryAssetPickCandidates(ray, *geometry, proxy.worldTransform, lineToleranceWorld, meshHits);
        for (const detail::MeshPickResult& meshHit : meshHits) {
            if (meshHit.distance) {
                results.push_back(detail::pickResultFromMeshHit(proxy.entity, proxy, meshHit, lineToleranceWorld));
            }
        }
    });
    return results;
}

void sortPicks(std::vector<RenderScene::PickResult>& picks) {
    std::sort(picks.begin(), picks.end(), [](const RenderScene::PickResult& lhs, const RenderScene::PickResult& rhs) {
        if (lhs.entity.value != rhs.entity.value) {
            return lhs.entity.value < rhs.entity.value;
        }
        if (lhs.sourceDrawableIndex != rhs.sourceDrawableIndex) {
            return lhs.sourceDrawableIndex < rhs.sourceDrawableIndex;
        }
        if (lhs.primitiveIndex != rhs.primitiveIndex) {
            return lhs.primitiveIndex < rhs.primitiveIndex;
        }
        return lhs.distance < rhs.distance;
    });
}

TEST(RenderSceneSpatialIndexTests, IndexedResultsMatchLinearReference) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets);
    scene::Scene source;
    for (size_t i = 0; i < 24u; ++i) {
        const scene::EntityId entity = addCurveEntity(source, line->id(), static_cast<double>(i) * 2.0 - 22.0);
        if (i % 7u == 0u) {
            ASSERT_TRUE(source.setVisible(entity, false));
        }
    }

    RenderScene renderScene;
    renderScene.sync(source, assets);

    for (double x : { -22.0, -18.0, -4.0, 0.0, 16.0, 25.0 }) {
        const math::Ray3 ray = verticalRay(x);
        const auto expected = linearPick(renderScene, assets, ray, 0.05);
        PickQueryStats stats;
        const auto actual = renderScene.pick(ray, 0.05, &stats);
        ASSERT_EQ(actual.has_value(), expected.has_value());
        if (actual) {
            EXPECT_EQ(actual->entity, expected->entity);
            EXPECT_EQ(actual->kind, expected->kind);
            EXPECT_NEAR(actual->distance, expected->distance, 1e-12);
        }

        auto expectedCandidates = linearCollect(renderScene, assets, ray, 0.05);
        std::vector<RenderScene::PickResult> actualCandidates;
        renderScene.collectPickCandidates(ray, 0.05, actualCandidates);
        sortPicks(expectedCandidates);
        sortPicks(actualCandidates);
        ASSERT_EQ(actualCandidates.size(), expectedCandidates.size());
        for (size_t i = 0; i < actualCandidates.size(); ++i) {
            EXPECT_EQ(actualCandidates[i].entity, expectedCandidates[i].entity);
            EXPECT_EQ(actualCandidates[i].kind, expectedCandidates[i].kind);
            EXPECT_NEAR(actualCandidates[i].distance, expectedCandidates[i].distance, 1e-12);
        }
        EXPECT_LE(stats.candidateProxyCount, stats.visibleProxyCount);
    }
}

TEST(RenderSceneSpatialIndexTests, SyncUpdatesIndexAfterTransformVisibilityAndDeletion) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets);
    scene::Scene source;
    const scene::EntityId entity = addCurveEntity(source, line->id(), 0.0);

    RenderScene renderScene;
    renderScene.sync(source, assets);
    ASSERT_TRUE(renderScene.pick(verticalRay(0.0), 0.01));

    ASSERT_TRUE(source.setWorldTransform(entity, math::Mat4::translate(math::Vec3(10.0, 0.0, 0.0))));
    renderScene.sync(source, assets);
    EXPECT_FALSE(renderScene.pick(verticalRay(0.0), 0.01));
    ASSERT_TRUE(renderScene.pick(verticalRay(10.0), 0.01));

    ASSERT_TRUE(source.setVisible(entity, false));
    renderScene.sync(source, assets);
    PickQueryStats hiddenStats;
    EXPECT_FALSE(renderScene.pick(verticalRay(10.0), 0.01, &hiddenStats));
    EXPECT_EQ(hiddenStats.visibleProxyCount, 0u);

    ASSERT_TRUE(source.setVisible(entity, true));
    renderScene.sync(source, assets);
    ASSERT_TRUE(renderScene.pick(verticalRay(10.0), 0.01));

    source.destroyEntity(entity);
    renderScene.sync(source, assets);
    PickQueryStats deletedStats;
    EXPECT_FALSE(renderScene.pick(verticalRay(10.0), 0.01, &deletedStats));
    EXPECT_EQ(deletedStats.visibleProxyCount, 0u);
    EXPECT_EQ(renderScene.proxy(entity), nullptr);
}

TEST(RenderSceneSpatialIndexTests, GeometryRevisionRefreshesBoundsWithoutSceneDirtyFlags) {
    asset::AssetLibrary assets;
    auto* curve = assets.create<asset::CurveAsset>("MutableLine");
    const asset::CurveElementId element =
            curve->addSegment(math::Segment3(math::Point3(-0.4, 0.0, 0.0), math::Point3(0.4, 0.0, 0.0)));
    scene::Scene source;
    const scene::EntityId entity = addCurveEntity(source, curve->id(), 0.0);

    RenderScene renderScene;
    renderScene.sync(source, assets);
    const uint64_t previousGeometryGeneration = renderScene.geometryGeneration();
    const scene::SceneRevision sceneRevision = source.revision();
    ASSERT_FALSE(renderScene.pick(verticalRay(10.0), 0.01));

    ASSERT_TRUE(
            curve->updateSegment(element, math::Segment3(math::Point3(9.6, 0.0, 0.0), math::Point3(10.4, 0.0, 0.0))));
    ASSERT_EQ(source.revision(), sceneRevision);
    renderScene.sync(source, assets);

    EXPECT_GT(renderScene.geometryGeneration(), previousGeometryGeneration);
    EXPECT_FALSE(renderScene.pick(verticalRay(0.0), 0.01));
    const auto movedHit = renderScene.pick(verticalRay(10.0), 0.01);
    ASSERT_TRUE(movedHit);
    EXPECT_EQ(movedHit->entity, entity);
}

TEST(RenderSceneSpatialIndexTests, BroadPhaseSubstantiallyReducesLargeSceneCandidates) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets);
    scene::Scene source;
    scene::EntityId expected;
    for (size_t i = 0; i < 128u; ++i) {
        const double x = (static_cast<double>(i) - 64.0) * 3.0;
        const scene::EntityId entity = addCurveEntity(source, line->id(), x);
        if (x == 0.0) {
            expected = entity;
        }
    }

    RenderScene renderScene;
    renderScene.sync(source, assets);
    PickQueryStats stats;
    const auto hit = renderScene.pick(verticalRay(0.0), 0.01, &stats);

    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->entity, expected);
    EXPECT_EQ(stats.visibleProxyCount, 128u);
    EXPECT_EQ(stats.indexedProxyCount, 128u);
    EXPECT_EQ(stats.fallbackProxyCount, 0u);
    EXPECT_LT(stats.candidateProxyCount, stats.visibleProxyCount / 8u);
    EXPECT_EQ(stats.exactAssetTestCount, stats.candidateProxyCount);
    EXPECT_LT(stats.nodeBoundsTestCount, stats.visibleProxyCount);
}

TEST(RenderSceneSpatialIndexTests, LineToleranceExpandsNodeAndLeafBoundsWithoutMissingBoundaryHit) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets, 1.0);
    scene::Scene source;
    const scene::EntityId entity = addCurveEntity(source, line->id(), 0.0);

    RenderScene renderScene;
    renderScene.sync(source, assets);

    PickQueryStats stats;
    const auto boundaryHit = renderScene.pick(verticalRay(0.0, 0.5), 0.5, &stats);
    ASSERT_TRUE(boundaryHit);
    EXPECT_EQ(boundaryHit->entity, entity);
    EXPECT_EQ(stats.nodeBoundsTestCount, 1u);
    EXPECT_EQ(stats.leafBoundsTestCount, 1u);
    EXPECT_EQ(stats.candidateProxyCount, 1u);
    EXPECT_EQ(stats.exactAssetTestCount, 1u);
    EXPECT_FALSE(renderScene.pick(verticalRay(0.0, 0.5), 0.49));

    const auto zeroTolerance = renderScene.pick(verticalRay(0.0), 0.0);
    const auto negativeTolerance = renderScene.pick(verticalRay(0.0), -1.0);
    ASSERT_TRUE(zeroTolerance);
    ASSERT_TRUE(negativeTolerance);
    EXPECT_EQ(negativeTolerance->entity, zeroTolerance->entity);
    EXPECT_EQ(negativeTolerance->kind, zeroTolerance->kind);
    EXPECT_DOUBLE_EQ(negativeTolerance->distance, zeroTolerance->distance);
    EXPECT_DOUBLE_EQ(negativeTolerance->toleranceWorld, 0.0);
}

TEST(RenderSceneSpatialIndexTests, EmptyBoundsAlwaysReachConservativeExactFallback) {
    asset::AssetLibrary assets;
    auto* line = assets.create<EmptyBoundsCurveAsset>("EmptyBoundsLine");
    line->addSegment(math::Segment3(math::Point3(-1.0, 0.0, 0.0), math::Point3(1.0, 0.0, 0.0)));
    scene::Scene source;
    const scene::EntityId entity = addCurveEntity(source, line->id(), 0.0);

    RenderScene renderScene;
    renderScene.sync(source, assets);
    ASSERT_TRUE(renderScene.proxy(entity)->worldBounds.isEmpty());

    PickQueryStats stats;
    const auto hit = renderScene.pick(verticalRay(0.0), 0.01, &stats);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->entity, entity);
    EXPECT_EQ(stats.visibleProxyCount, 1u);
    EXPECT_EQ(stats.indexedProxyCount, 0u);
    EXPECT_EQ(stats.fallbackProxyCount, 1u);
    EXPECT_EQ(stats.candidateProxyCount, 1u);
    EXPECT_EQ(stats.exactAssetTestCount, 1u);

    std::vector<RenderScene::PickResult> candidates;
    PickQueryStats collectStats;
    renderScene.collectPickCandidates(verticalRay(0.0), 0.01, candidates, &collectStats);
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates.front().entity, entity);
    EXPECT_EQ(collectStats.fallbackProxyCount, 1u);
    EXPECT_EQ(collectStats.exactAssetTestCount, 1u);
}

TEST(RenderSceneSpatialIndexTests, IndependentRenderScenesConsumeTheSameJournal) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets);
    scene::Scene source;
    const scene::EntityId entity = addCurveEntity(source, line->id(), 0.0);

    RenderScene firstConsumer;
    RenderScene secondConsumer;
    firstConsumer.sync(source, assets);
    secondConsumer.sync(source, assets);

    ASSERT_TRUE(source.setWorldTransform(entity, math::Mat4::translate(math::Vec3(8.0, 0.0, 0.0))));
    firstConsumer.sync(source, assets);
    EXPECT_TRUE(firstConsumer.pick(verticalRay(8.0), 0.01));
    EXPECT_FALSE(firstConsumer.pick(verticalRay(0.0), 0.01));

    // 第一个消费者推进自己的 cursor 后，第二个消费者仍能独立读取同一批变化。
    EXPECT_TRUE(secondConsumer.pick(verticalRay(0.0), 0.01));
    secondConsumer.sync(source, assets);
    EXPECT_TRUE(secondConsumer.pick(verticalRay(8.0), 0.01));
    EXPECT_FALSE(secondConsumer.pick(verticalRay(0.0), 0.01));
}

TEST(RenderSceneSpatialIndexTests, JournalOverflowRestoresTheCompleteCurrentState) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets);
    scene::Scene source(2);
    const scene::EntityId entity = addCurveEntity(source, line->id(), 0.0);

    RenderScene renderScene;
    renderScene.sync(source, assets);

    // 三次发布超过容量；Selection 已离开保留窗口，增量读取必须拒绝残缺结果并全量恢复。
    ASSERT_TRUE(source.setSelected(entity, true));
    ASSERT_TRUE(source.setName(entity, "Renamed"));
    ASSERT_TRUE(source.setWorldTransform(entity, math::Mat4::translate(math::Vec3(6.0, 0.0, 0.0))));
    renderScene.sync(source, assets);

    const SceneProxy* proxy = renderScene.proxy(entity);
    ASSERT_NE(proxy, nullptr);
    EXPECT_TRUE(proxy->selected);
    EXPECT_TRUE(renderScene.pick(verticalRay(6.0), 0.01));
    EXPECT_FALSE(renderScene.pick(verticalRay(0.0), 0.01));
}

TEST(RenderSceneSpatialIndexTests, AssetMembershipChangeRestoresPreviouslyMissingProxy) {
    asset::AssetLibrary assets;
    scene::Scene source;
    const scene::EntityId entity = source.createEntity("PendingGeometry");
    ASSERT_TRUE(source.setGeometry(entity, asset::AssetId{ 1 }));

    RenderScene renderScene;
    renderScene.sync(source, assets);
    ASSERT_EQ(renderScene.proxy(entity), nullptr);
    EXPECT_EQ(renderScene.lastSyncStats().missingGeometryCount, 1u);
    renderScene.sync(source, assets);
    EXPECT_EQ(renderScene.lastSyncStats().missingGeometryCount, 1u);

    asset::CurveAsset* line = makeLineAsset(assets);
    ASSERT_EQ(line->id(), asset::AssetId{ 1 });
    renderScene.sync(source, assets);
    ASSERT_NE(renderScene.proxy(entity), nullptr);
    EXPECT_EQ(renderScene.lastSyncStats().missingGeometryCount, 0u);
    EXPECT_TRUE(renderScene.pick(verticalRay(0.0), 0.01));

    ASSERT_TRUE(assets.remove(line->id()));
    renderScene.sync(source, assets);
    EXPECT_EQ(renderScene.proxy(entity), nullptr);
    EXPECT_EQ(renderScene.lastSyncStats().missingGeometryCount, 1u);
}

TEST(RenderSceneSpatialIndexTests, LightChangesFollowJournalWithoutGeometryProxy) {
    asset::AssetLibrary assets;
    scene::Scene source;
    const scene::EntityId entity = source.createEntity("Light");

    RenderScene renderScene;
    renderScene.sync(source, assets);
    ASSERT_TRUE(renderScene.lights().empty());

    scene::LightComponent light;
    light.kind = scene::LightKind::Point;
    ASSERT_TRUE(source.setLight(entity, light));
    renderScene.sync(source, assets);
    ASSERT_EQ(renderScene.lights().size(), 1u);

    ASSERT_TRUE(source.setWorldTransform(entity, math::Mat4::translate(math::Vec3{ 5.0, 0.0, 0.0 })));
    renderScene.sync(source, assets);
    ASSERT_EQ(renderScene.lights().size(), 1u);
    EXPECT_DOUBLE_EQ(renderScene.lights().front().position.x, 5.0);

    ASSERT_TRUE(source.removeLight(entity));
    renderScene.sync(source, assets);
    EXPECT_TRUE(renderScene.lights().empty());
}

TEST(RenderSceneSpatialIndexTests, LightDirectionIgnoresInheritedNonUniformScale) {
    asset::AssetLibrary assets;
    scene::Scene source;
    const scene::EntityId parent = source.createEntity("ScaledParent");
    const scene::EntityId lightEntity = source.createEntity("RotatedLight");
    constexpr double kQuarterTurn = 0.7853981633974483;

    ASSERT_TRUE(source.setLocalTransform(parent, math::Mat4::scale(math::Vec3(2.0, 1.0, 1.0))));
    ASSERT_TRUE(source.setLocalTransform(lightEntity, math::Mat4::rotation(math::Vec3::unitY(), kQuarterTurn)));
    ASSERT_TRUE(source.setParent(lightEntity, parent));
    scene::LightComponent light;
    light.kind = scene::LightKind::Spot;
    ASSERT_TRUE(source.setLight(lightEntity, light));

    RenderScene renderScene;
    renderScene.sync(source, assets);

    ASSERT_EQ(renderScene.lights().size(), 1u);
    const math::Vec3 expected = math::Mat3::rotation(math::Vec3::unitY(), kQuarterTurn) * math::Vec3(0.0, 0.0, -1.0);
    EXPECT_NEAR(renderScene.lights().front().direction.x, expected.x, 1.0e-12);
    EXPECT_NEAR(renderScene.lights().front().direction.y, expected.y, 1.0e-12);
    EXPECT_NEAR(renderScene.lights().front().direction.z, expected.z, 1.0e-12);

    ASSERT_TRUE(source.setLocalTransform(parent, math::Mat4::scale(math::Vec3(3.0, 0.5, 1.5))));
    renderScene.sync(source, assets);
    ASSERT_EQ(renderScene.lights().size(), 1u);
    EXPECT_NEAR(renderScene.lights().front().direction.x, expected.x, 1.0e-12);
    EXPECT_NEAR(renderScene.lights().front().direction.y, expected.y, 1.0e-12);
    EXPECT_NEAR(renderScene.lights().front().direction.z, expected.z, 1.0e-12);
}

TEST(RenderSceneSpatialIndexTests, LightSelectionIsDeterministicAndKeepsDirectionalLightsFirst) {
    asset::AssetLibrary assets;
    scene::Scene source;
    for (uint32_t index = 0; index < 10u; ++index) {
        const scene::EntityId entity = source.createEntity("PointLight");
        scene::LightComponent light;
        light.kind = scene::LightKind::Point;
        light.intensity = static_cast<double>(index + 1u);
        ASSERT_TRUE(source.setLight(entity, light));
    }
    const scene::EntityId directionalEntity = source.createEntity("DirectionalLight");
    scene::LightComponent directional;
    directional.kind = scene::LightKind::Directional;
    directional.intensity = 0.25;
    ASSERT_TRUE(source.setLight(directionalEntity, directional));

    RenderScene renderScene;
    renderScene.sync(source, assets);

    ASSERT_EQ(renderScene.lights().size(), engine::LightEnvironment::kMaxLights);
    EXPECT_EQ(renderScene.lights().front().type, engine::LightType::Directional);
    EXPECT_DOUBLE_EQ(renderScene.lights()[1].intensity, 10.0);
    EXPECT_DOUBLE_EQ(renderScene.lights()[2].intensity, 9.0);
}

TEST(RenderSceneSpatialIndexTests, ReusedEntitySlotReceivesANewStablePickId) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets);
    scene::Scene source;
    const scene::EntityId oldEntity = addCurveEntity(source, line->id(), 0.0);

    RenderScene renderScene;
    renderScene.sync(source, assets);
    const SceneProxy* oldProxy = renderScene.proxy(oldEntity);
    ASSERT_NE(oldProxy, nullptr);
    const engine::PickId oldPickId = oldProxy->pickId;
    ASSERT_TRUE(oldPickId);

    source.destroyEntity(oldEntity);
    const scene::EntityId newEntity = addCurveEntity(source, line->id(), 4.0);
    ASSERT_EQ(newEntity.index(), oldEntity.index());
    ASSERT_NE(newEntity.generation(), oldEntity.generation());
    renderScene.sync(source, assets);

    EXPECT_EQ(renderScene.proxy(oldEntity), nullptr);
    const SceneProxy* newProxy = renderScene.proxy(newEntity);
    ASSERT_NE(newProxy, nullptr);
    EXPECT_NE(newProxy->pickId, oldPickId);
    const auto hit = renderScene.pick(verticalRay(4.0), 0.01);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->entity, newEntity);
    EXPECT_EQ(hit->pickId, newProxy->pickId);

    const engine::PickId stablePickId = newProxy->pickId;
    renderScene.resetSync();
    renderScene.sync(source, assets);
    ASSERT_NE(renderScene.proxy(newEntity), nullptr);
    EXPECT_EQ(renderScene.proxy(newEntity)->pickId, stablePickId);
}

TEST(RenderSceneSpatialIndexTests, SceneRebuiltAtSameAddressDoesNotReusePickId) {
    asset::AssetLibrary assets;
    asset::CurveAsset* line = makeLineAsset(assets);
    std::optional<scene::Scene> source;
    source.emplace();
    scene::Scene* const sourceAddress = &*source;
    const scene::EntityId oldEntity = addCurveEntity(*source, line->id(), 0.0);

    RenderScene renderScene;
    renderScene.sync(*source, assets);
    const SceneProxy* oldProxy = renderScene.proxy(oldEntity);
    ASSERT_NE(oldProxy, nullptr);
    const engine::PickId oldPickId = oldProxy->pickId;
    ASSERT_TRUE(oldPickId);

    source.reset();
    source.emplace();
    ASSERT_EQ(&*source, sourceAddress);
    const scene::EntityId newEntity = addCurveEntity(*source, line->id(), 3.0);
    ASSERT_EQ(newEntity, oldEntity);

    renderScene.sync(*source, assets);
    const SceneProxy* newProxy = renderScene.proxy(newEntity);
    ASSERT_NE(newProxy, nullptr);
    EXPECT_NE(newProxy->pickId, oldPickId);
    const auto hit = renderScene.pick(verticalRay(3.0), 0.01);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->entity, newEntity);
    EXPECT_EQ(hit->pickId, newProxy->pickId);
}

}  // namespace
}  // namespace mulan::view
