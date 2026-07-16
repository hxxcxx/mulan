/**
 * @file render_world_sync_incremental_tests.cpp
 * @brief 验证 SceneProxy 精确脏传播、RenderWorld 空间快路径与缺失资产定向恢复。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include "scene_sync/render_world_sync.h"

#include <mulan/asset/asset_library.h>
#include <mulan/render/asset_gpu_key.h>
#include <mulan/scene/scene.h>
#include <mulan/view/scene_sync/render_scene.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mulan::view {
namespace {

class CountingGeometryAsset final : public asset::GeometryAsset {
public:
    CountingGeometryAsset(asset::AssetId id, std::string name, asset::AssetId material = {})
        : GeometryAsset(id, asset::AssetKind::Mesh, std::move(name)), material_(material) {
        mesh_.layout = graphics::layouts::surface();
        mesh_.topology = graphics::PrimitiveTopology::TriangleList;
        mesh_.vertices.resize(static_cast<size_t>(mesh_.layout.stride()) * 3u);
        mesh_.computeBounds();
        local_bounds_ = mesh_.bounds;
    }

    void collectDrawables(std::vector<asset::Drawable>& out) const override {
        ++collect_drawables_count_;
        out.push_back(asset::Drawable{ &mesh_, material_, asset::DrawableRole::Solid });
    }

    math::AABB3 localBounds() const override {
        ++local_bounds_count_;
        return local_bounds_;
    }

    void setLocalBounds(math::AABB3 bounds) {
        touch();
        local_bounds_ = std::move(bounds);
    }

    size_t collectDrawablesCount() const { return collect_drawables_count_; }
    size_t localBoundsCount() const { return local_bounds_count_; }

private:
    graphics::Mesh mesh_;
    math::AABB3 local_bounds_;
    asset::AssetId material_;
    mutable size_t collect_drawables_count_ = 0;
    mutable size_t local_bounds_count_ = 0;
};

scene::EntityId addEntity(scene::Scene& scene, asset::AssetId geometry) {
    const scene::EntityId entity = scene.createEntity("IncrementalEntity");
    EXPECT_TRUE(scene.setGeometry(entity, geometry));
    return entity;
}

TEST(RenderWorldSyncIncrementalTests, PlacementAndVisibilityDoNotReprojectGeometryOrMaterials) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<CountingGeometryAsset>("CountingGeometry");
    scene::Scene source;
    const scene::EntityId entity = addEntity(source, geometry->id());

    RenderScene renderScene;
    renderScene.sync(source, assets);
    ASSERT_EQ(geometry->localBoundsCount(), 1u);
    const RenderSceneChangeCursor placementCursor = renderScene.currentChangeCursor();
    const uint64_t geometryGeneration = renderScene.geometryGeneration();

    RenderWorldSync sync;
    engine::RenderWorld world;
    engine::RenderResourcePrepareList prepare;
    const engine::ResourceDomainId domain = engine::resourceDomainForAssetLibrary(assets.domainId());
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    ASSERT_EQ(geometry->collectDrawablesCount(), 1u);
    const size_t stableLocalBoundsCount = geometry->localBoundsCount();
    const engine::RenderWorldSnapshot first = world.snapshot();
    ASSERT_EQ(first.objects().size(), 1u);
    ASSERT_EQ(first.geometries().size(), 1u);
    ASSERT_EQ(first.materials().size(), 1u);
    const engine::RenderObjectId stableObject = first.objects().front().id;
    const engine::GeometryHandle stableGeometry = first.geometries().front().handle;
    const engine::RenderMaterialHandle stableMaterial = first.materials().front().handle;

    ASSERT_TRUE(source.setWorldTransform(entity, math::Mat4::translate(math::Vec3{ 8.0, 2.0, 0.0 })));
    renderScene.sync(source, assets);
    EXPECT_EQ(renderScene.geometryGeneration(), geometryGeneration);
    EXPECT_EQ(geometry->localBoundsCount(), stableLocalBoundsCount);
    const RenderSceneChangeSet placementChanges = renderScene.readChanges(placementCursor);
    ASSERT_EQ(placementChanges.status, RenderSceneChangeStatus::Changes);
    ASSERT_EQ(placementChanges.changes.size(), 1u);
    EXPECT_EQ(placementChanges.changes.front().entity, entity);
    EXPECT_EQ(placementChanges.changes.front().dirty, RenderProxyDirty::Placement);

    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    EXPECT_TRUE(prepare.empty());
    EXPECT_EQ(geometry->collectDrawablesCount(), 1u);
    EXPECT_EQ(sync.lastStats().patchedObjectCount, 1u);
    EXPECT_EQ(sync.lastStats().updatedObjectCount, 1u);
    EXPECT_EQ(sync.lastStats().sceneItems.accepted, 0u);
    EXPECT_FALSE(sync.lastStats().fullRebuild);
    const engine::RenderWorldSnapshot moved = world.snapshot();
    ASSERT_EQ(moved.objects().size(), 1u);
    EXPECT_EQ(moved.objects().front().id, stableObject);
    EXPECT_EQ(moved.geometries().front().handle, stableGeometry);
    EXPECT_EQ(moved.materials().front().handle, stableMaterial);
    EXPECT_EQ(math::Point3::origin().transformedBy(moved.objects().front().desc.worldTransform),
              math::Point3(8.0, 2.0, 0.0));
    EXPECT_EQ(math::Point3::origin().transformedBy(first.objects().front().desc.worldTransform),
              math::Point3::origin());

    const RenderSceneChangeCursor visibilityCursor = renderScene.currentChangeCursor();
    ASSERT_TRUE(source.setVisible(entity, false));
    renderScene.sync(source, assets);
    EXPECT_EQ(geometry->localBoundsCount(), stableLocalBoundsCount);
    const RenderSceneChangeSet visibilityChanges = renderScene.readChanges(visibilityCursor);
    ASSERT_EQ(visibilityChanges.status, RenderSceneChangeStatus::Changes);
    ASSERT_EQ(visibilityChanges.changes.size(), 1u);
    EXPECT_EQ(visibilityChanges.changes.front().dirty, RenderProxyDirty::Visibility);

    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    EXPECT_TRUE(prepare.empty());
    EXPECT_EQ(geometry->collectDrawablesCount(), 1u);
    EXPECT_EQ(sync.lastStats().patchedObjectCount, 1u);
    EXPECT_EQ(sync.lastStats().updatedObjectCount, 1u);
    EXPECT_EQ(sync.lastStats().sceneItems.accepted, 0u);
    const engine::RenderWorldSnapshot hidden = world.snapshot();
    ASSERT_EQ(hidden.objects().size(), 1u);
    EXPECT_EQ(hidden.objects().front().id, stableObject);
    EXPECT_FALSE(hidden.objects().front().desc.visible);
}

TEST(RenderWorldSyncIncrementalTests, MissingMaterialCreationOnlyWakesItsRealWaiter) {
    asset::AssetLibrary assets;
    constexpr asset::AssetId waitingMaterial{ 3 };
    auto* geometry = assets.create<CountingGeometryAsset>("Geometry", waitingMaterial);
    scene::Scene source;
    addEntity(source, geometry->id());

    RenderScene renderScene;
    renderScene.sync(source, assets);
    RenderWorldSync sync;
    engine::RenderWorld world;
    engine::RenderResourcePrepareList prepare;
    const engine::ResourceDomainId domain = engine::resourceDomainForAssetLibrary(assets.domainId());
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    ASSERT_FALSE(sync.referencedAssetsChanged(assets));
    ASSERT_EQ(world.snapshot().materials().size(), 1u);
    EXPECT_EQ(world.snapshot().materials().front().desc.resourceKey, engine::defaultRenderMaterialResourceKey());

    auto* unrelated = assets.create<asset::TextureAsset>("UnrelatedTexture");
    ASSERT_EQ(unrelated->id(), asset::AssetId{ 2 });
    EXPECT_FALSE(sync.referencedAssetsChanged(assets));

    auto* material = assets.create<asset::MaterialAsset>("RecoveredMaterial");
    ASSERT_EQ(material->id(), waitingMaterial);
    material->setBaseColor(math::Vec3{ 0.1, 0.2, 0.3 });
    EXPECT_TRUE(sync.referencedAssetsChanged(assets));

    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    EXPECT_EQ(sync.lastStats().patchedObjectCount, 1u);
    EXPECT_FALSE(sync.lastStats().fullRebuild);
    EXPECT_TRUE(prepare.geometries().empty());
    EXPECT_EQ(geometry->collectDrawablesCount(), 2u);
    const engine::RenderWorldSnapshot recovered = world.snapshot();
    ASSERT_EQ(recovered.materials().size(), 1u);
    EXPECT_EQ(recovered.materials().front().desc.resourceKey,
              engine::makeRenderResourceKey(domain, waitingMaterial.value, engine::RenderResourceKind::Material));
    EXPECT_EQ(recovered.materials().front().desc.material.baseColor, math::Vec3(0.1, 0.2, 0.3));
}

TEST(RenderWorldSyncIncrementalTests, MissingTextureCreationIsTrackedAsAnUnresolvedDependency) {
    asset::AssetLibrary assets;
    constexpr asset::AssetId materialId{ 2 };
    constexpr asset::AssetId waitingTexture{ 3 };
    auto* geometry = assets.create<CountingGeometryAsset>("Geometry", materialId);
    auto* material = assets.create<asset::MaterialAsset>("Material");
    ASSERT_EQ(material->id(), materialId);
    material->setBaseColorTexture(waitingTexture);
    scene::Scene source;
    addEntity(source, geometry->id());

    RenderScene renderScene;
    renderScene.sync(source, assets);
    RenderWorldSync sync;
    engine::RenderWorld world;
    engine::RenderResourcePrepareList prepare;
    const engine::ResourceDomainId domain = engine::resourceDomainForAssetLibrary(assets.domainId());
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    ASSERT_FALSE(sync.referencedAssetsChanged(assets));
    ASSERT_EQ(world.snapshot().materials().size(), 1u);
    EXPECT_FALSE(world.snapshot().materials().front().desc.baseColorTexture.resourceKey);

    auto* texture = assets.create<asset::TextureAsset>("RecoveredTexture");
    ASSERT_EQ(texture->id(), waitingTexture);
    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    ASSERT_TRUE(image);
    texture->setImage(image);
    EXPECT_TRUE(sync.referencedAssetsChanged(assets));

    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    EXPECT_EQ(sync.lastStats().patchedObjectCount, 1u);
    EXPECT_FALSE(sync.lastStats().fullRebuild);
    EXPECT_TRUE(prepare.geometries().empty());
    ASSERT_EQ(prepare.textures().size(), 1u);
    EXPECT_TRUE(prepare.textures().front().isUpsert());
    const engine::RenderWorldSnapshot recovered = world.snapshot();
    ASSERT_EQ(recovered.materials().size(), 1u);
    const engine::RenderTextureDesc& recoveredTexture = recovered.materials().front().desc.baseColorTexture;
    EXPECT_EQ(recoveredTexture.resourceKey,
              engine::makeRenderResourceKey(domain, waitingTexture.value, engine::RenderResourceKind::Texture));
    EXPECT_EQ(recoveredTexture.image, image);
}

TEST(RenderWorldSyncIncrementalTests, UnrelatedAssetPollingAdvancesItsBoundedJournalCursor) {
    asset::AssetLibrary assets(2);
    auto* geometry = assets.create<CountingGeometryAsset>("Geometry");
    scene::Scene source;
    addEntity(source, geometry->id());
    RenderScene renderScene;
    renderScene.sync(source, assets);

    RenderWorldSync sync;
    engine::RenderWorld world;
    engine::RenderResourcePrepareList prepare;
    const engine::ResourceDomainId domain = engine::resourceDomainForAssetLibrary(assets.domainId());
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    ASSERT_FALSE(sync.referencedAssetsChanged(assets));

    for (size_t index = 0; index < 3u; ++index) {
        assets.create<asset::MaterialAsset>("UnrelatedMaterial");
        EXPECT_FALSE(sync.referencedAssetsChanged(assets));
    }
}

TEST(RenderWorldSyncIncrementalTests, DirectGeometryContentChangeRefreshesWorldBoundsConservatively) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<CountingGeometryAsset>("Geometry");
    scene::Scene source;
    const scene::EntityId entity = addEntity(source, geometry->id());
    ASSERT_TRUE(source.setWorldTransform(entity, math::Mat4::translate(math::Vec3{ 10.0, 0.0, 0.0 })));
    RenderScene renderScene;
    renderScene.sync(source, assets);

    RenderWorldSync sync;
    engine::RenderWorld world;
    engine::RenderResourcePrepareList prepare;
    const engine::ResourceDomainId domain = engine::resourceDomainForAssetLibrary(assets.domainId());
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    const math::AABB3 oldProxyBounds = renderScene.proxy(entity)->worldBounds;

    geometry->setLocalBounds(math::AABB3{ math::Point3{ -2.0, -1.0, 0.0 }, math::Point3{ 4.0, 3.0, 2.0 } });
    ASSERT_TRUE(sync.referencedAssetsChanged(assets));
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);

    const engine::RenderWorldSnapshot updated = world.snapshot();
    ASSERT_EQ(updated.objects().size(), 1u);
    const math::AABB3& worldBounds = updated.objects().front().desc.worldBounds;
    EXPECT_EQ(worldBounds.min, math::Point3(8.0, -1.0, 0.0));
    EXPECT_EQ(worldBounds.max, math::Point3(14.0, 3.0, 2.0));
    EXPECT_EQ(renderScene.proxy(entity)->worldBounds.min, oldProxyBounds.min);
    EXPECT_EQ(renderScene.proxy(entity)->worldBounds.max, oldProxyBounds.max);
}

TEST(RenderWorldSyncIncrementalTests, SlowConsumerUsesCurrentProxyAfterRemovedThenAdded) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<CountingGeometryAsset>("Geometry");
    scene::Scene source;
    const scene::EntityId entity = addEntity(source, geometry->id());
    RenderScene renderScene;
    renderScene.sync(source, assets);

    RenderWorldSync sync;
    engine::RenderWorld world;
    engine::RenderResourcePrepareList prepare;
    const engine::ResourceDomainId domain = engine::resourceDomainForAssetLibrary(assets.domainId());
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    ASSERT_EQ(world.snapshot().objects().size(), 1u);
    const engine::RenderObjectId stableObject = world.snapshot().objects().front().id;

    ASSERT_TRUE(source.setGeometry(entity, asset::AssetId::invalid()));
    renderScene.sync(source, assets);
    ASSERT_EQ(renderScene.proxy(entity), nullptr);
    ASSERT_TRUE(source.setGeometry(entity, geometry->id()));
    renderScene.sync(source, assets);
    ASSERT_NE(renderScene.proxy(entity), nullptr);

    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    ASSERT_EQ(world.snapshot().objects().size(), 1u);
    EXPECT_EQ(world.snapshot().objects().front().id, stableObject);
    EXPECT_EQ(sync.lastStats().patchedObjectCount, 1u);
    EXPECT_EQ(sync.lastStats().updatedObjectCount, 1u);
}

TEST(RenderWorldSyncIncrementalTests, SlowConsumerUsesCurrentProxyAfterAddedThenRemoved) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<CountingGeometryAsset>("Geometry");
    scene::Scene source;
    const scene::EntityId entity = source.createEntity("InitiallyMissing");
    RenderScene renderScene;
    renderScene.sync(source, assets);

    RenderWorldSync sync;
    engine::RenderWorld world;
    engine::RenderResourcePrepareList prepare;
    const engine::ResourceDomainId domain = engine::resourceDomainForAssetLibrary(assets.domainId());
    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    ASSERT_EQ(world.objectCount(), 0u);

    ASSERT_TRUE(source.setGeometry(entity, geometry->id()));
    renderScene.sync(source, assets);
    ASSERT_NE(renderScene.proxy(entity), nullptr);
    ASSERT_TRUE(source.setGeometry(entity, asset::AssetId::invalid()));
    renderScene.sync(source, assets);
    ASSERT_EQ(renderScene.proxy(entity), nullptr);

    sync.rebuildScene(renderScene, assets, domain, world, &prepare);
    EXPECT_EQ(world.objectCount(), 0u);
    EXPECT_EQ(sync.lastStats().patchedObjectCount, 1u);
    EXPECT_EQ(sync.lastStats().addedObjectCount, 0u);
    EXPECT_EQ(sync.lastStats().removedObjectCount, 0u);
}

TEST(RenderWorldSpatialPatchTests, PreservesStableFieldsAndPublishedSnapshots) {
    engine::RenderWorld world;
    engine::RenderObjectDesc desc;
    desc.pickId = engine::PickId::fromValue(17);
    desc.selected = true;
    desc.visible = true;
    desc.drawables.push_back(
            { engine::GeometryHandle{ 4, 2 }, engine::RenderMaterialHandle{ 5, 3 }, engine::RenderBucket::Edge, 9 });
    const engine::RenderObjectId object = world.addObject(desc);
    const engine::RenderWorldSnapshot published = world.snapshot();

    const math::Mat4 transform = math::Mat4::translate(math::Vec3{ 3.0, 4.0, 5.0 });
    const math::AABB3 bounds{ math::Point3{ 2.0, 3.0, 4.0 }, math::Point3{ 4.0, 5.0, 6.0 } };
    ASSERT_TRUE(world.updateObjectSpatialState(object, transform, bounds, false));
    const engine::RenderWorldSnapshot current = world.snapshot();
    const engine::RenderObjectRecord* updated = current.object(object);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->desc.pickId, desc.pickId);
    EXPECT_TRUE(updated->desc.selected);
    EXPECT_FALSE(updated->desc.visible);
    ASSERT_EQ(updated->desc.drawables.size(), 1u);
    EXPECT_EQ(updated->desc.drawables.front().geometry, desc.drawables.front().geometry);
    EXPECT_EQ(updated->desc.drawables.front().material, desc.drawables.front().material);
    EXPECT_EQ(updated->desc.drawables.front().bucket, desc.drawables.front().bucket);
    EXPECT_EQ(updated->desc.drawables.front().sourceDrawableIndex, desc.drawables.front().sourceDrawableIndex);
    EXPECT_EQ(math::Point3::origin().transformedBy(updated->desc.worldTransform), math::Point3(3.0, 4.0, 5.0));
    ASSERT_NE(published.object(object), nullptr);
    EXPECT_TRUE(published.object(object)->desc.visible);

    const engine::RenderWorldVersion beforeNoOp = current.version();
    EXPECT_TRUE(world.updateObjectSpatialState(object, transform, bounds, false));
    EXPECT_EQ(world.snapshot().version(), beforeNoOp);
    const engine::RenderObjectId stale{ object.index, object.generation + 1u };
    EXPECT_FALSE(world.updateObjectSpatialState(stale, transform, bounds, true));
    EXPECT_EQ(world.snapshot().version(), beforeNoOp);
}

}  // namespace
}  // namespace mulan::view
