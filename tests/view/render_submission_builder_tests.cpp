/**
 * @file render_submission_builder_tests.cpp
 * @brief 验证 GPU 资源差量、批次确认语义与预览资源键的稳定性。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "scene_sync/render_submission_builder.h"
#include "scene_sync/render_item_builder.h"

#include <mulan/asset/asset_library.h>
#include <mulan/scene/scene.h>
#include <mulan/view/core/preview_layer.h>
#include <mulan/view/scene_sync/render_scene.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::view {
namespace {

graphics::Mesh makePreviewMesh() {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::position3();
    mesh.topology = graphics::PrimitiveTopology::LineList;
    mesh.vertices.resize(static_cast<size_t>(mesh.layout.stride()) * 2u);
    return mesh;
}

graphics::Mesh makeSurfaceMesh(uint8_t marker = 0) {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.topology = graphics::PrimitiveTopology::TriangleList;
    mesh.vertices.resize(static_cast<size_t>(mesh.layout.stride()) * 3u);
    mesh.vertices.back() = static_cast<std::byte>(marker);
    mesh.computeBounds();
    return mesh;
}

graphics::Mesh makeWireMesh(uint8_t marker = 0) {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::position3();
    mesh.topology = graphics::PrimitiveTopology::LineList;
    mesh.vertices.resize(static_cast<size_t>(mesh.layout.stride()) * 2u);
    mesh.vertices.back() = static_cast<std::byte>(marker);
    mesh.computeBounds();
    return mesh;
}

struct OverlayRoleProjection {
    engine::RenderMaterialHandle material;
    std::vector<engine::RenderObjectId> objects;
    std::vector<engine::GeometryHandle> geometries;
    std::vector<engine::RenderResourceKey> geometryKeys;
    std::vector<engine::RenderBucket> buckets;
};

OverlayRoleProjection overlayRoleProjection(const engine::RenderWorldSnapshot& snapshot, PreviewVisualRole role) {
    OverlayRoleProjection projection;
    for (const engine::RenderMaterialRecord& material : snapshot.materials()) {
        if (material.desc.resourceKey.kind == engine::RenderResourceKind::PreviewMaterial &&
            material.desc.resourceKey.source == RenderItemBuilder::previewMaterialKey(role)) {
            projection.material = material.handle;
            break;
        }
    }
    if (!projection.material) {
        return projection;
    }

    for (const engine::RenderObjectRecord& object : snapshot.objects()) {
        bool belongsToRole = false;
        for (const engine::RenderObjectDrawable& drawable : object.desc.drawables) {
            if (drawable.material != projection.material) {
                continue;
            }
            belongsToRole = true;
            projection.geometries.push_back(drawable.geometry);
            projection.buckets.push_back(drawable.bucket);
            if (const engine::RenderGeometryRecord* geometry = snapshot.geometry(drawable.geometry)) {
                projection.geometryKeys.push_back(geometry->desc.resourceKey);
            }
        }
        if (belongsToRole) {
            projection.objects.push_back(object.id);
        }
    }
    return projection;
}

TEST(PreviewLayerTests, GenerationAdvancesOnlyForRealChanges) {
    PreviewLayer preview;
    const uint64_t initial = preview.generation();

    preview.setSnapGeometry({}, { makePreviewMesh() });
    EXPECT_GT(preview.generation(), initial);
    ASSERT_EQ(preview.drawables(PreviewVisualRole::Snap).size(), 1u);
    EXPECT_TRUE(preview.drawables(PreviewVisualRole::Tool).empty());

    const PreviewReference reference{ .role = PreviewVisualRole::Tool };
    const uint64_t beforeReference = preview.generation();
    preview.setReferences({ reference });
    EXPECT_GT(preview.generation(), beforeReference);
    const uint64_t unchanged = preview.generation();
    preview.setReferences({ reference });
    EXPECT_EQ(preview.generation(), unchanged);
}

TEST(RenderResourcePrepareListTests, MergeUsesLastOperationForSameGeometryKey) {
    const engine::AssetGpuKey key = engine::makeAssetGpuKey(42);

    engine::RenderResourcePrepareList addThenRetire;
    addThenRetire.addGeometry(key, makeSurfaceMesh());
    engine::RenderResourcePrepareList retire;
    retire.retireGeometry(key);
    addThenRetire.merge(retire);

    ASSERT_EQ(addThenRetire.size(), 1u);
    EXPECT_TRUE(addThenRetire.geometries().front().isRetire());
    EXPECT_FALSE(addThenRetire.geometries().front().mesh);

    engine::RenderResourcePrepareList retireThenAdd;
    retireThenAdd.retireGeometry(key);
    engine::RenderResourcePrepareList add;
    add.addGeometry(key, makeSurfaceMesh(1));
    retireThenAdd.merge(add);

    ASSERT_EQ(retireThenAdd.size(), 1u);
    const engine::RenderGeometryPrepareDesc& final = retireThenAdd.geometries().front();
    EXPECT_TRUE(final.isUpsert());
    EXPECT_TRUE(final.forceUpdate);
    ASSERT_TRUE(final.mesh);
    EXPECT_EQ(final.mesh->vertices.back(), std::byte{ 1 });
}

TEST(RenderResourcePrepareListTests, MergeUsesLastOperationForCompleteTextureIdentity) {
    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    ASSERT_TRUE(image);
    const engine::RenderTextureResourceKey linearKey{
        .resourceKey = engine::makeAssetGpuKey(43),
        .srgb = false,
        .generateMips = true,
    };
    const engine::RenderTextureResourceKey srgbKey{
        .resourceKey = linearKey.resourceKey,
        .srgb = true,
        .generateMips = true,
    };

    engine::RenderResourcePrepareList addThenRetire;
    addThenRetire.addTexture(linearKey, image, 7);
    engine::RenderResourcePrepareList retire;
    retire.retireTexture(linearKey);
    addThenRetire.merge(retire);

    ASSERT_EQ(addThenRetire.textures().size(), 1u);
    EXPECT_TRUE(addThenRetire.textures().front().isRetire());
    EXPECT_FALSE(addThenRetire.textures().front().image);

    engine::RenderResourcePrepareList retireThenAdd;
    retireThenAdd.retireTexture(linearKey);
    engine::RenderResourcePrepareList add;
    add.addTexture(linearKey, image, 8);
    // 同 AssetGpuKey 的 sRGB 实例是独立资源，不应被线性实例的操作覆盖。
    add.addTexture(srgbKey, image, 8);
    retireThenAdd.merge(add);

    ASSERT_EQ(retireThenAdd.textures().size(), 2u);
    const engine::RenderTexturePrepareDesc& restored = retireThenAdd.textures().front();
    EXPECT_EQ(restored.identity, linearKey);
    EXPECT_TRUE(restored.isUpsert());
    EXPECT_EQ(restored.contentRevision, 8u);
    EXPECT_EQ(restored.image, image);
}

TEST(RenderResourceDomainTests, EqualAssetIdsFromDifferentLibrariesProduceDifferentGpuKeys) {
    asset::AssetLibrary firstAssets;
    asset::AssetLibrary secondAssets;
    auto* firstMesh = firstAssets.create<asset::MeshAsset>("FirstMesh");
    auto* secondMesh = secondAssets.create<asset::MeshAsset>("SecondMesh");
    ASSERT_EQ(firstMesh->id(), secondMesh->id());
    firstMesh->addPrimitive(makeSurfaceMesh());
    secondMesh->addPrimitive(makeSurfaceMesh());

    scene::Scene firstScene;
    scene::Scene secondScene;
    const scene::EntityId firstEntity = firstScene.createEntity("First");
    const scene::EntityId secondEntity = secondScene.createEntity("Second");
    ASSERT_TRUE(firstScene.setGeometry(firstEntity, firstMesh->id()));
    ASSERT_TRUE(secondScene.setGeometry(secondEntity, secondMesh->id()));
    RenderScene firstRenderScene;
    RenderScene secondRenderScene;
    firstRenderScene.sync(firstScene, firstAssets);
    secondRenderScene.sync(secondScene, secondAssets);

    RenderSubmissionBuilder firstBuilder;
    RenderSubmissionBuilder secondBuilder;
    firstBuilder.setScene(&firstRenderScene, &firstAssets);
    secondBuilder.setScene(&secondRenderScene, &secondAssets);
    const ViewState view;
    const RenderSubmission first = firstBuilder.build(view);
    const RenderSubmission second = secondBuilder.build(view);
    ASSERT_EQ(first.prepare.geometries().size(), 1u);
    ASSERT_EQ(second.prepare.geometries().size(), 1u);
    const engine::RenderResourceKey firstKey = first.prepare.geometries().front().resourceKey;
    const engine::RenderResourceKey secondKey = second.prepare.geometries().front().resourceKey;
    EXPECT_NE(firstKey, secondKey);
    EXPECT_NE(firstKey.domain, secondKey.domain);
    EXPECT_EQ(firstKey.source, secondKey.source);
    EXPECT_EQ(firstKey.subresource, secondKey.subresource);
    EXPECT_EQ(firstKey.kind, engine::RenderResourceKind::Geometry);
    EXPECT_EQ(secondKey.kind, engine::RenderResourceKind::Geometry);
}

TEST(RenderResourceDomainTests, BuildersForTheSameAssetLibraryReuseTheDocumentDomain) {
    asset::AssetLibrary assets;
    auto* mesh = assets.create<asset::MeshAsset>("Mesh");
    mesh->addPrimitive(makeSurfaceMesh());
    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("Entity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, mesh->id()));
    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder firstBuilder;
    RenderSubmissionBuilder secondBuilder;
    firstBuilder.setScene(&renderScene, &assets);
    secondBuilder.setScene(&renderScene, &assets);
    const ViewState view;
    const RenderSubmission first = firstBuilder.build(view);
    const RenderSubmission second = secondBuilder.build(view);
    ASSERT_EQ(first.prepare.geometries().size(), 1u);
    ASSERT_EQ(second.prepare.geometries().size(), 1u);
    EXPECT_EQ(first.prepare.geometries().front().resourceKey, second.prepare.geometries().front().resourceKey);
}

TEST(RenderResourceDomainTests, ReusedAssetLibraryAddressCannotReuseThePreviousGpuDomain) {
    std::optional<asset::AssetLibrary> assets(std::in_place);
    asset::AssetLibrary* const stableAddress = &*assets;
    auto* firstMesh = assets->create<asset::MeshAsset>("FirstGeneration");
    firstMesh->addPrimitive(makeSurfaceMesh(1));
    const asset::AssetId stableAssetId = firstMesh->id();

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("Entity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, stableAssetId));
    RenderScene renderScene;
    renderScene.sync(sourceScene, *assets);

    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &*assets);
    const RenderSubmission first = builder.build(ViewState{});
    ASSERT_EQ(first.prepare.geometries().size(), 1u);
    const engine::RenderResourceKey firstKey = first.prepare.geometries().front().resourceKey;
    builder.acknowledgeResources(first.resourceBatchId);

    assets.reset();
    assets.emplace();
    ASSERT_EQ(&*assets, stableAddress);
    auto* replacementMesh = assets->create<asset::MeshAsset>("SecondGeneration");
    ASSERT_EQ(replacementMesh->id(), stableAssetId);
    replacementMesh->addPrimitive(makeSurfaceMesh(77));
    renderScene.sync(sourceScene, *assets);

    // 指针、AssetId、asset revision 都可与上一代相同；domain 必须仍能识别换代。
    builder.setScene(&renderScene, &*assets);
    const RenderSubmission replacement = builder.build(ViewState{});
    ASSERT_EQ(replacement.prepare.geometries().size(), 2u);
    const auto retired = std::ranges::find_if(replacement.prepare.geometries(),
                                              [&](const auto& resource) { return resource.resourceKey == firstKey; });
    ASSERT_NE(retired, replacement.prepare.geometries().end());
    EXPECT_TRUE(retired->isRetire());
    const auto preparedIt = std::ranges::find_if(replacement.prepare.geometries(),
                                                 [](const auto& resource) { return resource.isUpsert(); });
    ASSERT_NE(preparedIt, replacement.prepare.geometries().end());
    const engine::RenderGeometryPrepareDesc& prepared = *preparedIt;
    EXPECT_NE(prepared.resourceKey.domain, firstKey.domain);
    ASSERT_TRUE(prepared.mesh);
    EXPECT_EQ(prepared.mesh->vertices.back(), std::byte{ 77 });
}

TEST(RenderSubmissionBuilderTests, ReusedRenderSceneAddressCannotReuseThePreviousWorlds) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::MeshAsset>("StableGeometry");
    geometry->addPrimitive(makeSurfaceMesh());

    scene::Scene firstSource;
    const scene::EntityId referencedEntity = firstSource.createEntity("First");
    ASSERT_TRUE(firstSource.setGeometry(referencedEntity, geometry->id()));

    std::optional<RenderScene> renderScene(std::in_place);
    RenderScene* const stableAddress = &*renderScene;
    renderScene->sync(firstSource, assets);
    const uint64_t collidingGeneration = renderScene->generation();

    PreviewLayer preview;
    preview.setReferences({ PreviewReference{ .entity = referencedEntity, .role = PreviewVisualRole::Tool } });
    RenderSubmissionBuilder builder;
    builder.setScene(&*renderScene, &assets);
    builder.setPreviewLayer(&preview);
    const RenderSubmission first = builder.build(ViewState{});
    ASSERT_TRUE(first.sceneWorld);
    ASSERT_TRUE(first.overlayWorld);
    ASSERT_EQ(first.sceneWorld->objects().size(), 1u);

    renderScene.reset();
    renderScene.emplace();
    ASSERT_EQ(&*renderScene, stableAddress);
    scene::Scene replacementSource;
    const scene::EntityId replacementReferenced = replacementSource.createEntity("Replacement");
    ASSERT_EQ(replacementReferenced, referencedEntity);
    ASSERT_TRUE(replacementSource.setGeometry(replacementReferenced, geometry->id()));
    ASSERT_TRUE(replacementSource.setWorldTransform(replacementReferenced,
                                                    math::Mat4::translate(math::Vec3(7.0, 0.0, 0.0))));
    const scene::EntityId secondEntity = replacementSource.createEntity("Second");
    ASSERT_TRUE(replacementSource.setGeometry(secondEntity, geometry->id()));
    renderScene->sync(replacementSource, assets);
    ASSERT_EQ(renderScene->generation(), collidingGeneration);

    builder.setScene(&*renderScene, &assets);
    const RenderSubmission replacement = builder.build(ViewState{});
    ASSERT_TRUE(replacement.sceneWorld);
    ASSERT_TRUE(replacement.overlayWorld);
    EXPECT_NE(replacement.sceneWorld, first.sceneWorld);
    EXPECT_EQ(replacement.sceneWorld->objects().size(), 2u);
    ASSERT_EQ(replacement.overlayWorld->objects().size(), 1u);
    EXPECT_EQ(math::Point3::origin().transformedBy(replacement.overlayWorld->objects().front().desc.worldTransform),
              math::Point3(7.0, 0.0, 0.0));
}

TEST(RenderSubmissionBuilderTests, KeepsPendingResourcesUntilMatchingAck) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.hasResourceUpdates());
    ASSERT_NE(first.resourceBatchId, 0u);
    ASSERT_EQ(first.prepare.size(), 1u);

    const RenderSubmission repeated = builder.build(view);
    EXPECT_TRUE(repeated.hasResourceUpdates());
    EXPECT_EQ(repeated.resourceBatchId, first.resourceBatchId);
    EXPECT_EQ(repeated.prepare.size(), first.prepare.size());

    builder.acknowledgeResources(first.resourceBatchId + 1u);
    EXPECT_TRUE(builder.build(view).hasResourceUpdates());

    builder.acknowledgeResources(first.resourceBatchId);
    const RenderSubmission acknowledged = builder.build(view);
    EXPECT_FALSE(acknowledged.hasResourceUpdates());
    EXPECT_TRUE(acknowledged.prepare.empty());
}

TEST(RenderSubmissionBuilderTests, NewPreviewRevisionSupersedesPendingBatch) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    graphics::Mesh updatedMesh = makePreviewMesh();
    updatedMesh.vertices.back() = std::byte{ 1 };
    preview.setMesh(std::move(updatedMesh));

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.hasResourceUpdates());

    preview.setMesh(makePreviewMesh());
    const RenderSubmission updated = builder.build(view);
    ASSERT_TRUE(updated.hasResourceUpdates());
    EXPECT_NE(updated.resourceBatchId, first.resourceBatchId);
    ASSERT_EQ(first.prepare.size(), 1u);
    ASSERT_EQ(updated.prepare.size(), 1u);
    EXPECT_EQ(updated.prepare.geometries().front().resourceKey, first.prepare.geometries().front().resourceKey);
    EXPECT_TRUE(updated.prepare.geometries().front().forceUpdate);

    builder.acknowledgeResources(first.resourceBatchId);
    EXPECT_TRUE(builder.build(view).hasResourceUpdates());

    builder.acknowledgeResources(updated.resourceBatchId);
    EXPECT_FALSE(builder.build(view).hasResourceUpdates());
}

TEST(RenderSubmissionBuilderTests, PreviewReferenceOnlyRevisionDoesNotUploadUnchangedPreviewGeometry) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 1u);
    builder.acknowledgeResources(first.resourceBatchId);

    // references 变化会推进 PreviewLayer generation，但不应让未变的直接预览 mesh 重传。
    preview.setReferences({ PreviewReference{} });
    const RenderSubmission referencesChanged = builder.build(view);
    EXPECT_EQ(referencesChanged.sceneWorld, first.sceneWorld);
    EXPECT_NE(referencesChanged.overlayWorld, first.overlayWorld);
    EXPECT_TRUE(referencesChanged.prepare.empty());
}

TEST(RenderSubmissionBuilderTests, PreviewAndViewChangesReuseSceneWorld) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::TessellatedAsset>("StableScene");
    geometry->setRenderMeshes(makeSurfaceMesh(), {});

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("StableEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());

    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    builder.setPreviewLayer(&preview);

    ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.sceneWorld);
    ASSERT_TRUE(first.overlayWorld);
    const auto stableSceneWorld = first.sceneWorld;

    preview.setMesh(makePreviewMesh());
    const RenderSubmission previewChanged = builder.build(view);
    EXPECT_EQ(previewChanged.sceneWorld, stableSceneWorld);
    EXPECT_NE(previewChanged.overlayWorld, first.overlayWorld);

    view.hoveredPickId = engine::PickId{ 7 };
    const RenderSubmission viewChanged = builder.build(view);
    EXPECT_EQ(viewChanged.sceneWorld, stableSceneWorld);
    EXPECT_EQ(viewChanged.overlayWorld, previewChanged.overlayWorld);
}

TEST(RenderSubmissionBuilderTests, PreviewReferencesBorrowSceneResourcesWithoutRetiringThem) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::TessellatedAsset>("ReferencedScene");
    geometry->setRenderMeshes(makeSurfaceMesh(), {});

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("ReferencedEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    PreviewLayer preview;

    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    builder.setPreviewLayer(&preview);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.geometries().size(), 1u);
    const engine::AssetGpuKey sceneGeometryKey = first.prepare.geometries().front().resourceKey;
    builder.acknowledgeResources(first.resourceBatchId);

    preview.setReferences({ PreviewReference{ .entity = entity } });
    const RenderSubmission referenced = builder.build(view);
    ASSERT_TRUE(referenced.overlayWorld);
    EXPECT_TRUE(referenced.prepare.empty());

    preview.clearReferences();
    const RenderSubmission cleared = builder.build(view);
    for (const auto& resource : cleared.prepare.geometries()) {
        EXPECT_FALSE(resource.resourceKey == sceneGeometryKey && resource.isRetire());
    }
}

TEST(RenderSubmissionBuilderTests, ReplacingPreviewSourceWithSameGenerationUpdatesOnlyPreviewGeometry) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer firstPreview;
    PreviewLayer secondPreview;
    firstPreview.setMesh(makePreviewMesh());
    graphics::Mesh replacement = makePreviewMesh();
    replacement.vertices.back() = std::byte{ 1 };
    secondPreview.setMesh(std::move(replacement));
    ASSERT_EQ(firstPreview.generation(), secondPreview.generation());

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&firstPreview);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 1u);
    const engine::AssetGpuKey key = first.prepare.geometries().front().resourceKey;
    builder.acknowledgeResources(first.resourceBatchId);

    builder.setPreviewLayer(&secondPreview);
    const RenderSubmission replaced = builder.build(view);

    ASSERT_EQ(replaced.prepare.size(), 1u);
    const engine::RenderGeometryPrepareDesc& update = replaced.prepare.geometries().front();
    EXPECT_EQ(update.resourceKey, key);
    EXPECT_TRUE(update.isUpsert());
    EXPECT_TRUE(update.forceUpdate);
}

TEST(RenderSubmissionBuilderTests, UpdatingOnePreviewRoleRebuildsOverlayAndPreservesStableResourceKeys) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());
    preview.setSnapGeometry({}, { makePreviewMesh() });

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.overlayWorld);
    const OverlayRoleProjection firstTool = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Tool);
    const OverlayRoleProjection firstSnap = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Snap);
    ASSERT_EQ(firstTool.objects.size(), 1u);
    ASSERT_EQ(firstTool.geometries.size(), 1u);
    ASSERT_EQ(firstSnap.objects.size(), 1u);
    ASSERT_EQ(firstSnap.geometries.size(), 1u);
    builder.acknowledgeResources(first.resourceBatchId);

    graphics::Mesh replacement = makePreviewMesh();
    replacement.vertices.back() = std::byte{ 9 };
    preview.setMesh(std::move(replacement));
    const RenderSubmission updated = builder.build(view);
    ASSERT_TRUE(updated.overlayWorld);
    EXPECT_TRUE(builder.lastOverlayStats().fullRebuild);
    EXPECT_EQ(builder.lastOverlayStats().patchedObjectCount, 1u);
    const OverlayRoleProjection updatedTool = overlayRoleProjection(*updated.overlayWorld, PreviewVisualRole::Tool);
    const OverlayRoleProjection updatedSnap = overlayRoleProjection(*updated.overlayWorld, PreviewVisualRole::Snap);

    // OverlayWorld 整体重建，前端句柄换代；GPU 资源 key 保持稳定且只更新真实变化项。
    EXPECT_NE(updatedTool.objects, firstTool.objects);
    EXPECT_NE(updatedTool.geometries, firstTool.geometries);
    EXPECT_NE(updatedSnap.objects, firstSnap.objects);
    EXPECT_NE(updatedSnap.geometries, firstSnap.geometries);
    EXPECT_EQ(updatedSnap.geometryKeys, firstSnap.geometryKeys);
    EXPECT_EQ(updatedSnap.buckets, firstSnap.buckets);
    ASSERT_EQ(updated.prepare.geometries().size(), 1u);
    EXPECT_EQ(updated.prepare.geometries().front().resourceKey, firstTool.geometryKeys.front());
    EXPECT_TRUE(updated.prepare.geometries().front().isUpsert());
    EXPECT_TRUE(updated.prepare.geometries().front().forceUpdate);
}

TEST(RenderSubmissionBuilderTests, ClearingOnePreviewRoleRemovesOnlyThatRoleAndRetiresOnlyItsGeometry) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());
    preview.setSnapGeometry({}, { makePreviewMesh() });

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.overlayWorld);
    const OverlayRoleProjection tool = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Tool);
    const OverlayRoleProjection snap = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Snap);
    ASSERT_EQ(tool.geometryKeys.size(), 1u);
    ASSERT_EQ(snap.objects.size(), 1u);
    builder.acknowledgeResources(first.resourceBatchId);

    preview.clearToolGeometry();
    const RenderSubmission cleared = builder.build(view);
    ASSERT_TRUE(cleared.overlayWorld);
    EXPECT_TRUE(builder.lastOverlayStats().fullRebuild);
    EXPECT_TRUE(overlayRoleProjection(*cleared.overlayWorld, PreviewVisualRole::Tool).objects.empty());
    const OverlayRoleProjection stableSnap = overlayRoleProjection(*cleared.overlayWorld, PreviewVisualRole::Snap);
    EXPECT_NE(stableSnap.objects, snap.objects);
    EXPECT_NE(stableSnap.geometries, snap.geometries);
    EXPECT_EQ(stableSnap.geometryKeys, snap.geometryKeys);
    ASSERT_EQ(cleared.prepare.geometries().size(), 1u);
    EXPECT_EQ(cleared.prepare.geometries().front().resourceKey, tool.geometryKeys.front());
    EXPECT_TRUE(cleared.prepare.geometries().front().isRetire());
}

TEST(RenderSubmissionBuilderTests, ReferenceOnlyChangesRebuildOverlayWithoutUploadingUnchangedDirectGeometry) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::TessellatedAsset>("ReferenceOnlyRole");
    geometry->setRenderMeshes(makeSurfaceMesh(), {});
    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("Referenced");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));
    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);

    PreviewLayer preview;
    preview.setSnapGeometry({}, { makePreviewMesh() });
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    builder.setPreviewLayer(&preview);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.overlayWorld);
    const OverlayRoleProjection firstSnap = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Snap);
    builder.acknowledgeResources(first.resourceBatchId);

    preview.setReferences({ PreviewReference{ .entity = entity, .role = PreviewVisualRole::Tool } });
    const RenderSubmission referenced = builder.build(view);
    ASSERT_TRUE(referenced.overlayWorld);
    EXPECT_TRUE(referenced.prepare.empty());
    const OverlayRoleProjection referencedSnap =
            overlayRoleProjection(*referenced.overlayWorld, PreviewVisualRole::Snap);
    const OverlayRoleProjection referencedTool =
            overlayRoleProjection(*referenced.overlayWorld, PreviewVisualRole::Tool);
    EXPECT_NE(referencedSnap.objects, firstSnap.objects);
    EXPECT_NE(referencedSnap.geometries, firstSnap.geometries);
    EXPECT_EQ(referencedSnap.geometryKeys, firstSnap.geometryKeys);
    ASSERT_EQ(referencedTool.objects.size(), 1u);

    preview.setReferences({ PreviewReference{
            .entity = entity,
            .worldTransform = math::Mat4::translate(math::Vec3(3.0, 0.0, 0.0)),
            .overrideWorldTransform = true,
            .role = PreviewVisualRole::Tool,
    } });
    const RenderSubmission transformed = builder.build(view);
    EXPECT_TRUE(transformed.prepare.empty());
    ASSERT_TRUE(transformed.overlayWorld);
    const OverlayRoleProjection transformedTool =
            overlayRoleProjection(*transformed.overlayWorld, PreviewVisualRole::Tool);
    EXPECT_NE(transformedTool.objects, referencedTool.objects);
    EXPECT_NE(transformedTool.geometries, referencedTool.geometries);
    EXPECT_EQ(overlayRoleProjection(*transformed.overlayWorld, PreviewVisualRole::Snap).geometryKeys,
              firstSnap.geometryKeys);
}

TEST(RenderSubmissionBuilderTests, UnrelatedAssetEventsAdvanceOverlayCursorWithoutOverflowRebuild) {
    asset::AssetLibrary assets(2);
    auto* geometry = assets.create<asset::TessellatedAsset>("ReferencedGeometry");
    geometry->setRenderMeshes(makeSurfaceMesh(), {});
    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("Referenced");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));
    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);

    PreviewLayer preview;
    preview.setReferences({ PreviewReference{ .entity = entity, .role = PreviewVisualRole::Tool } });
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    builder.setPreviewLayer(&preview);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.overlayWorld);
    builder.acknowledgeResources(first.resourceBatchId);

    // journal 仅保留两项；若 Overlay 不独立确认无关事件，第三项会制造伪 overflow，
    // 从而把仍然稳定的引用角色误判为必须全量重投影。
    for (size_t index = 0; index < 3; ++index) {
        ASSERT_NE(assets.create<asset::MaterialAsset>("Unrelated" + std::to_string(index)), nullptr);
        const RenderSubmission unchanged = builder.build(view);
        EXPECT_EQ(unchanged.overlayWorld, first.overlayWorld);
    }
}

TEST(RenderSubmissionBuilderTests, AssetClearDoesNotRebuildOverlayWithoutReferenceState) {
    asset::AssetLibrary assets;
    ASSERT_NE(assets.create<asset::MaterialAsset>("Unrelated"), nullptr);
    RenderScene scene;
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());
    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.overlayWorld);
    builder.acknowledgeResources(first.resourceBatchId);

    assets.clear();
    const RenderSubmission cleared = builder.build(view);
    EXPECT_EQ(cleared.overlayWorld, first.overlayWorld);
}

TEST(RenderSubmissionBuilderTests, AssetClearRebuildsOverlayWithCurrentReferenceState) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::TessellatedAsset>("ReferencedGeometry");
    geometry->setRenderMeshes(makeSurfaceMesh(), {});
    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("Referenced");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));
    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);

    PreviewLayer preview;
    preview.setSnapGeometry({}, { makePreviewMesh() });
    preview.setReferences({ PreviewReference{ .entity = entity, .role = PreviewVisualRole::Tool } });
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    builder.setPreviewLayer(&preview);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.overlayWorld);
    const OverlayRoleProjection firstTool = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Tool);
    const OverlayRoleProjection firstSnap = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Snap);
    ASSERT_EQ(firstTool.objects.size(), 1u);
    ASSERT_EQ(firstSnap.objects.size(), 1u);
    builder.acknowledgeResources(first.resourceBatchId);

    assets.clear();
    const RenderSubmission cleared = builder.build(view);
    ASSERT_TRUE(cleared.overlayWorld);
    EXPECT_TRUE(overlayRoleProjection(*cleared.overlayWorld, PreviewVisualRole::Tool).objects.empty());
    const OverlayRoleProjection stableSnap = overlayRoleProjection(*cleared.overlayWorld, PreviewVisualRole::Snap);
    EXPECT_NE(stableSnap.objects, firstSnap.objects);
    EXPECT_NE(stableSnap.geometries, firstSnap.geometries);
    EXPECT_EQ(stableSnap.geometryKeys, firstSnap.geometryKeys);
    EXPECT_EQ(builder.lastOverlayStats().patchedObjectCount, 1u);
    EXPECT_TRUE(std::ranges::none_of(cleared.prepare.geometries(), [](const auto& resource) {
        return resource.resourceKey.kind == engine::RenderResourceKind::PreviewGeometry;
    }));
}

TEST(RenderSubmissionBuilderTests, ReplacingRoleGeometryPreparesOnlyChangedStableRoleSlots) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    preview.setGripGeometry({}, { makePreviewMesh() });
    preview.setSnapGeometry({}, { makePreviewMesh() });
    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.overlayWorld);
    const OverlayRoleProjection firstGrip = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Grip);
    const OverlayRoleProjection firstSnap = overlayRoleProjection(*first.overlayWorld, PreviewVisualRole::Snap);
    ASSERT_EQ(firstGrip.geometryKeys.size(), 1u);
    builder.acknowledgeResources(first.resourceBatchId);

    graphics::Mesh firstReplacement = makePreviewMesh();
    firstReplacement.vertices.back() = std::byte{ 1 };
    graphics::Mesh added = makePreviewMesh();
    added.vertices.back() = std::byte{ 2 };
    preview.setGripGeometry({}, { std::move(firstReplacement), std::move(added) });
    const RenderSubmission replaced = builder.build(view);
    ASSERT_TRUE(replaced.overlayWorld);
    const OverlayRoleProjection replacedGrip = overlayRoleProjection(*replaced.overlayWorld, PreviewVisualRole::Grip);
    EXPECT_NE(replacedGrip.objects, firstGrip.objects);
    ASSERT_EQ(replacedGrip.geometries.size(), 2u);
    EXPECT_NE(replacedGrip.geometries.front(), firstGrip.geometries.front());
    EXPECT_EQ(replacedGrip.geometryKeys.front(), firstGrip.geometryKeys.front());
    EXPECT_EQ(overlayRoleProjection(*replaced.overlayWorld, PreviewVisualRole::Snap).geometryKeys,
              firstSnap.geometryKeys);

    ASSERT_EQ(replaced.prepare.geometries().size(), 2u);
    EXPECT_TRUE(std::ranges::all_of(replaced.prepare.geometries(), [](const auto& resource) {
        return resource.isUpsert() && resource.resourceKey.kind == engine::RenderResourceKind::PreviewGeometry;
    }));
    const auto stableUpdate = std::ranges::find_if(replaced.prepare.geometries(), [&](const auto& resource) {
        return resource.resourceKey == firstGrip.geometryKeys.front();
    });
    ASSERT_NE(stableUpdate, replaced.prepare.geometries().end());
    EXPECT_TRUE(stableUpdate->forceUpdate);
}

TEST(RenderSubmissionBuilderTests, InvalidatingRenderThreadRebuildsCurrentResources) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.hasResourceUpdates());
    builder.acknowledgeResources(first.resourceBatchId);
    ASSERT_FALSE(builder.build(view).hasResourceUpdates());

    builder.invalidateResources();
    const RenderSubmission rebuilt = builder.build(view);
    EXPECT_TRUE(rebuilt.hasResourceUpdates());
    EXPECT_NE(rebuilt.resourceBatchId, first.resourceBatchId);
}

TEST(RenderSubmissionBuilderTests, TransformOnlyWorldRebuildDoesNotUploadGeometryAgain) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::TessellatedAsset>("TransformOnly");
    geometry->setRenderMeshes(makeSurfaceMesh(), {});

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TransformOnlyEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 1u);
    builder.acknowledgeResources(first.resourceBatchId);
    ASSERT_FALSE(builder.build(view).hasResourceUpdates());

    ASSERT_TRUE(sourceScene.setWorldTransform(entity, math::Mat4::translate(math::Vec3(2.0, 0.0, 0.0))));
    renderScene.sync(sourceScene, assets);
    const RenderSubmission transformed = builder.build(view);

    EXPECT_TRUE(transformed.prepare.empty());
    EXPECT_NE(transformed.sceneWorld, first.sceneWorld);
}

TEST(RenderSubmissionBuilderTests, UpdatingOneAssetOnlyUpsertsItsGeometryKey) {
    asset::AssetLibrary assets;
    auto* changedAsset = assets.create<asset::TessellatedAsset>("Changed");
    changedAsset->setRenderMeshes(makeSurfaceMesh(), {});
    auto* stableAsset = assets.create<asset::TessellatedAsset>("Stable");
    stableAsset->setRenderMeshes(makeSurfaceMesh(1), {});

    scene::Scene sourceScene;
    const scene::EntityId changedEntity = sourceScene.createEntity("ChangedEntity");
    const scene::EntityId stableEntity = sourceScene.createEntity("StableEntity");
    ASSERT_TRUE(sourceScene.setGeometry(changedEntity, changedAsset->id()));
    ASSERT_TRUE(sourceScene.setGeometry(stableEntity, stableAsset->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 2u);
    builder.acknowledgeResources(first.resourceBatchId);
    ASSERT_FALSE(builder.build(view).hasResourceUpdates());

    // 不修改 Scene，验证 builder 能直接观察被引用资产的内容版本。
    changedAsset->setRenderMeshes(makeSurfaceMesh(2), {});
    const RenderSubmission updated = builder.build(view);

    ASSERT_EQ(updated.prepare.size(), 1u);
    EXPECT_NE(updated.sceneWorld, first.sceneWorld);
    EXPECT_TRUE(updated.prepare.geometries().front().isUpsert());
    EXPECT_TRUE(updated.prepare.geometries().front().forceUpdate);
}

TEST(RenderSubmissionBuilderTests, RemovingOneDrawableOnlyRetiresItsKey) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::TessellatedAsset>("TwoDrawables");
    geometry->setRenderMeshes(makeSurfaceMesh(), makeWireMesh());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TwoDrawableEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 2u);
    builder.acknowledgeResources(first.resourceBatchId);

    geometry->setRenderMeshes(makeSurfaceMesh(), {});
    const RenderSubmission reduced = builder.build(view);

    ASSERT_EQ(reduced.prepare.size(), 1u);
    EXPECT_TRUE(reduced.prepare.geometries().front().isRetire());
}

TEST(RenderSubmissionBuilderTests, RemovingLastSceneReferenceRetiresGeometryKey) {
    asset::AssetLibrary assets;
    auto* geometry = assets.create<asset::TessellatedAsset>("Retired");
    geometry->setRenderMeshes(makeSurfaceMesh(), {});

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("RetiredEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 1u);
    const engine::AssetGpuKey key = first.prepare.geometries().front().resourceKey;
    builder.acknowledgeResources(first.resourceBatchId);

    sourceScene.destroyEntity(entity);
    renderScene.sync(sourceScene, assets);
    const RenderSubmission removed = builder.build(view);

    ASSERT_EQ(removed.prepare.size(), 1u);
    EXPECT_TRUE(removed.prepare.geometries().front().isRetire());
    EXPECT_EQ(removed.prepare.geometries().front().resourceKey, key);
}

TEST(RenderSubmissionBuilderTests, ClearingPreviewRetiresItsStableGeometryKey) {
    RenderScene scene;
    asset::AssetLibrary assets;
    PreviewLayer preview;
    preview.setMesh(makePreviewMesh());

    RenderSubmissionBuilder builder;
    builder.setScene(&scene, &assets);
    builder.setPreviewLayer(&preview);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 1u);
    const engine::AssetGpuKey key = first.prepare.geometries().front().resourceKey;
    builder.acknowledgeResources(first.resourceBatchId);

    preview.clear();
    const RenderSubmission cleared = builder.build(view);

    ASSERT_EQ(cleared.prepare.size(), 1u);
    EXPECT_TRUE(cleared.prepare.geometries().front().isRetire());
    EXPECT_EQ(cleared.prepare.geometries().front().resourceKey, key);
}

TEST(RenderSubmissionBuilderTests, MaterialRevisionRebuildsWorldWithoutGeometryUpload) {
    asset::AssetLibrary assets;
    auto* material = assets.create<asset::MaterialAsset>("MutableMaterial");
    auto* geometry = assets.create<asset::MeshAsset>("MaterialGeometry");
    geometry->addPrimitive(makeSurfaceMesh(), material->id());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("MaterialEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.prepare.size(), 1u);
    builder.acknowledgeResources(first.resourceBatchId);
    ASSERT_FALSE(builder.build(view).hasResourceUpdates());

    material->setRoughness(0.25);
    const RenderSubmission materialChanged = builder.build(view);

    EXPECT_TRUE(materialChanged.prepare.empty());
    EXPECT_NE(materialChanged.sceneWorld, first.sceneWorld);
}

TEST(RenderSubmissionBuilderTests, TextureRevisionFlowsIntoWorldWithoutGeometryUpload) {
    asset::AssetLibrary assets;
    auto* texture = assets.create<asset::TextureAsset>("MutableTexture");
    texture->setImage(core::Image::create(1, 1, core::PixelFormat::RGBA8));
    auto* material = assets.create<asset::MaterialAsset>("TexturedMaterial");
    material->setBaseColorTexture(texture->id());
    auto* geometry = assets.create<asset::MeshAsset>("TexturedGeometry");
    geometry->addPrimitive(makeSurfaceMesh(), material->id());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TexturedEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.sceneWorld);
    ASSERT_EQ(first.sceneWorld->materials().size(), 1u);
    ASSERT_EQ(first.sceneWorld->materials().front().desc.baseColorTexture.contentRevision, texture->revision());
    builder.acknowledgeResources(first.resourceBatchId);

    texture->setImage(core::Image::create(1, 1, core::PixelFormat::RGBA8));
    const RenderSubmission textureChanged = builder.build(view);

    EXPECT_TRUE(textureChanged.prepare.geometries().empty());
    EXPECT_NE(textureChanged.sceneWorld, first.sceneWorld);
    ASSERT_EQ(textureChanged.prepare.textures().size(), 1u);
    EXPECT_TRUE(textureChanged.prepare.textures().front().isUpsert());
    EXPECT_EQ(textureChanged.prepare.textures().front().contentRevision, texture->revision());
    ASSERT_TRUE(textureChanged.sceneWorld);
    ASSERT_EQ(textureChanged.sceneWorld->materials().size(), 1u);
    EXPECT_EQ(textureChanged.sceneWorld->materials().front().desc.baseColorTexture.contentRevision,
              texture->revision());
}

TEST(RenderSubmissionBuilderTests, RemovingLastTextureReferenceEmitsReliableRetire) {
    asset::AssetLibrary assets;
    auto* texture = assets.create<asset::TextureAsset>("RetiredTexture");
    texture->setImage(core::Image::create(1, 1, core::PixelFormat::RGBA8));
    auto* material = assets.create<asset::MaterialAsset>("TexturedMaterial");
    material->setBaseColorTexture(texture->id());
    auto* geometry = assets.create<asset::MeshAsset>("TexturedGeometry");
    geometry->addPrimitive(makeSurfaceMesh(), material->id());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TexturedEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission initial = builder.build(view);
    ASSERT_EQ(initial.prepare.textures().size(), 1u);
    const engine::RenderTextureResourceKey identity = initial.prepare.textures().front().identity;
    EXPECT_TRUE(initial.prepare.textures().front().isUpsert());
    builder.acknowledgeResources(initial.resourceBatchId);

    material->setBaseColorTexture(asset::AssetId::invalid());
    const RenderSubmission removed = builder.build(view);

    EXPECT_TRUE(removed.prepare.geometries().empty());
    EXPECT_NE(removed.sceneWorld, initial.sceneWorld);
    ASSERT_EQ(removed.prepare.textures().size(), 1u);
    EXPECT_TRUE(removed.prepare.textures().front().isRetire());
    EXPECT_EQ(removed.prepare.textures().front().identity, identity);
}

TEST(RenderSubmissionBuilderTests, RemovingReferencedTextureAssetEmitsReliableRetire) {
    asset::AssetLibrary assets;
    auto* texture = assets.create<asset::TextureAsset>("DeletedTexture");
    texture->setImage(core::Image::create(1, 1, core::PixelFormat::RGBA8));
    const asset::AssetId textureId = texture->id();
    auto* material = assets.create<asset::MaterialAsset>("TexturedMaterial");
    material->setBaseColorTexture(textureId);
    auto* geometry = assets.create<asset::MeshAsset>("TexturedGeometry");
    geometry->addPrimitive(makeSurfaceMesh(), material->id());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TexturedEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission initial = builder.build(view);
    ASSERT_EQ(initial.prepare.textures().size(), 1u);
    const engine::RenderTextureResourceKey identity = initial.prepare.textures().front().identity;
    builder.acknowledgeResources(initial.resourceBatchId);

    ASSERT_TRUE(assets.remove(textureId));
    const RenderSubmission removed = builder.build(view);

    ASSERT_EQ(removed.prepare.textures().size(), 1u);
    EXPECT_NE(removed.sceneWorld, initial.sceneWorld);
    EXPECT_TRUE(removed.prepare.textures().front().isRetire());
    EXPECT_EQ(removed.prepare.textures().front().identity, identity);
}

TEST(RenderSubmissionBuilderTests, PendingTextureRetireIsSupersededWhenReferenceReappears) {
    asset::AssetLibrary assets;
    auto* texture = assets.create<asset::TextureAsset>("RestoredTexture");
    texture->setImage(core::Image::create(1, 1, core::PixelFormat::RGBA8));
    const asset::AssetId textureId = texture->id();
    auto* material = assets.create<asset::MaterialAsset>("TexturedMaterial");
    material->setBaseColorTexture(textureId);
    auto* geometry = assets.create<asset::MeshAsset>("TexturedGeometry");
    geometry->addPrimitive(makeSurfaceMesh(), material->id());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TexturedEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission initial = builder.build(view);
    ASSERT_EQ(initial.prepare.textures().size(), 1u);
    builder.acknowledgeResources(initial.resourceBatchId);

    material->setBaseColorTexture(asset::AssetId::invalid());
    const RenderSubmission retired = builder.build(view);
    ASSERT_EQ(retired.prepare.textures().size(), 1u);
    ASSERT_TRUE(retired.prepare.textures().front().isRetire());

    material->setBaseColorTexture(textureId);
    const RenderSubmission restored = builder.build(view);
    ASSERT_NE(restored.resourceBatchId, retired.resourceBatchId);
    ASSERT_EQ(restored.prepare.textures().size(), 1u);
    EXPECT_TRUE(restored.prepare.textures().front().isUpsert());
    EXPECT_EQ(restored.prepare.textures().front().contentRevision, texture->revision());
    EXPECT_TRUE(restored.prepare.textures().front().image);

    // 迟到的旧 retire ACK 不得清除已覆盖它的新 upsert 批次。
    builder.acknowledgeResources(retired.resourceBatchId);
    const RenderSubmission afterStaleAck = builder.build(view);
    EXPECT_EQ(afterStaleAck.resourceBatchId, restored.resourceBatchId);
    ASSERT_EQ(afterStaleAck.prepare.textures().size(), 1u);
    EXPECT_TRUE(afterStaleAck.prepare.textures().front().isUpsert());
}

TEST(RenderSubmissionBuilderTests, TextureOptionChangeRetiresOnlyOldIdentityAndUpsertsNewIdentity) {
    asset::AssetLibrary assets;
    auto* texture = assets.create<asset::TextureAsset>("ColorTexture");
    texture->setImage(core::Image::create(1, 1, core::PixelFormat::RGBA8));
    auto* material = assets.create<asset::MaterialAsset>("TexturedMaterial");
    material->setBaseColorTexture(texture->id());
    auto* geometry = assets.create<asset::MeshAsset>("TexturedGeometry");
    geometry->addPrimitive(makeSurfaceMesh(), material->id());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TexturedEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission initial = builder.build(view);
    ASSERT_EQ(initial.prepare.textures().size(), 1u);
    const engine::RenderTextureResourceKey oldIdentity = initial.prepare.textures().front().identity;
    ASSERT_TRUE(oldIdentity.srgb);
    builder.acknowledgeResources(initial.resourceBatchId);

    material->setBaseColorTextureSrgb(false);
    const RenderSubmission changed = builder.build(view);

    ASSERT_EQ(changed.prepare.textures().size(), 2u);
    size_t retireCount = 0;
    size_t upsertCount = 0;
    for (const engine::RenderTexturePrepareDesc& texturePrepare : changed.prepare.textures()) {
        if (texturePrepare.isRetire()) {
            ++retireCount;
            EXPECT_EQ(texturePrepare.identity, oldIdentity);
        } else {
            ++upsertCount;
            EXPECT_EQ(texturePrepare.identity.resourceKey, oldIdentity.resourceKey);
            EXPECT_FALSE(texturePrepare.identity.srgb);
        }
    }
    EXPECT_EQ(retireCount, 1u);
    EXPECT_EQ(upsertCount, 1u);
}

TEST(RenderSubmissionBuilderTests, InvalidatingRenderThreadRestoresEveryLiveTextureIdentity) {
    asset::AssetLibrary assets;
    auto* texture = assets.create<asset::TextureAsset>("RestoredTexture");
    texture->setImage(core::Image::create(1, 1, core::PixelFormat::RGBA8));
    auto* material = assets.create<asset::MaterialAsset>("TexturedMaterial");
    material->setBaseColorTexture(texture->id());
    auto* geometry = assets.create<asset::MeshAsset>("TexturedGeometry");
    geometry->addPrimitive(makeSurfaceMesh(), material->id());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("TexturedEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, geometry->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission initial = builder.build(view);
    ASSERT_EQ(initial.prepare.textures().size(), 1u);
    const engine::RenderTextureResourceKey identity = initial.prepare.textures().front().identity;
    builder.acknowledgeResources(initial.resourceBatchId);
    ASSERT_FALSE(builder.build(view).hasResourceUpdates());

    builder.invalidateResources();
    const RenderSubmission restored = builder.build(view);

    ASSERT_EQ(restored.prepare.textures().size(), 1u);
    EXPECT_TRUE(restored.prepare.textures().front().isUpsert());
    EXPECT_EQ(restored.prepare.textures().front().identity, identity);
    EXPECT_EQ(restored.prepare.textures().front().contentRevision, texture->revision());
}

TEST(RenderSubmissionBuilderTests, InvalidatingRenderThreadFullyRestoresAllLiveGeometry) {
    asset::AssetLibrary assets;
    auto* firstAsset = assets.create<asset::TessellatedAsset>("First");
    firstAsset->setRenderMeshes(makeSurfaceMesh(), {});
    auto* secondAsset = assets.create<asset::TessellatedAsset>("Second");
    secondAsset->setRenderMeshes(makeSurfaceMesh(1), {});

    scene::Scene sourceScene;
    const scene::EntityId firstEntity = sourceScene.createEntity("FirstEntity");
    const scene::EntityId secondEntity = sourceScene.createEntity("SecondEntity");
    ASSERT_TRUE(sourceScene.setGeometry(firstEntity, firstAsset->id()));
    ASSERT_TRUE(sourceScene.setGeometry(secondEntity, secondAsset->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission initial = builder.build(view);
    ASSERT_EQ(initial.prepare.size(), 2u);
    builder.acknowledgeResources(initial.resourceBatchId);
    ASSERT_FALSE(builder.build(view).hasResourceUpdates());

    builder.invalidateResources();
    const RenderSubmission restored = builder.build(view);
    ASSERT_EQ(restored.prepare.size(), 2u);
    for (const engine::RenderGeometryPrepareDesc& geometryPrepare : restored.prepare.geometries()) {
        EXPECT_TRUE(geometryPrepare.isUpsert());
        EXPECT_TRUE(geometryPrepare.forceUpdate);
    }
}

TEST(RenderItemBuilderTests, PreviewGeometryKeysUseStableRoleLocalSlots) {
    std::vector<PreviewDrawable> mixed{
        PreviewDrawable{ makePreviewMesh(), PreviewVisualRole::Tool },
        PreviewDrawable{ makePreviewMesh(), PreviewVisualRole::Snap },
    };
    std::vector<RenderItem> mixedItems;
    RenderItemBuilder::buildPreviewItems(mixed, mixedItems);
    ASSERT_EQ(mixedItems.size(), 2u);

    std::vector<PreviewDrawable> snapOnly{
        PreviewDrawable{ makePreviewMesh(), PreviewVisualRole::Snap },
    };
    std::vector<RenderItem> snapItems;
    RenderItemBuilder::buildPreviewItems(snapOnly, snapItems);
    ASSERT_EQ(snapItems.size(), 1u);

    EXPECT_EQ(mixedItems[1].geometryKey, snapItems[0].geometryKey);
    EXPECT_NE(mixedItems[0].geometryKey, snapItems[0].geometryKey);
}

TEST(RenderSubmissionBuilderTests, MaterialLessDrawableKeepsStableDefaultIdentityAndHandleAcrossPatches) {
    asset::AssetLibrary assets;
    auto* meshAsset = assets.create<asset::MeshAsset>("MaterialLessMesh");
    meshAsset->addPrimitive(makeSurfaceMesh());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("MaterialLessEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, meshAsset->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);

    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.sceneWorld);
    ASSERT_EQ(first.sceneWorld->materials().size(), 1u);
    const engine::AssetGpuKey stableKey = first.sceneWorld->materials().front().desc.resourceKey;
    ASSERT_EQ(stableKey, engine::defaultRenderMaterialResourceKey());
    const engine::RenderMaterialHandle stableHandle = first.sceneWorld->materials().front().handle;

    for (size_t rebuild = 0; rebuild < 32u; ++rebuild) {
        ASSERT_TRUE(sourceScene.setWorldTransform(
                entity, math::Mat4::translate(math::Vec3(static_cast<double>(rebuild + 1u), 0.0, 0.0))));
        renderScene.sync(sourceScene, assets);

        const RenderSubmission submission = builder.build(view);
        ASSERT_TRUE(submission.sceneWorld);
        ASSERT_EQ(submission.sceneWorld->materials().size(), 1u);
        const auto& material = submission.sceneWorld->materials().front();
        EXPECT_EQ(material.desc.resourceKey, stableKey);
        EXPECT_EQ(material.handle, stableHandle);
        EXPECT_FALSE(builder.lastStats().fullRebuild);
        EXPECT_EQ(builder.lastStats().patchedObjectCount, 1u);
        EXPECT_EQ(builder.lastStats().updatedObjectCount, 1u);
    }
}

TEST(RenderSubmissionBuilderTests, SingleTransformPatchesOneObjectAndPreservesAllStableIds) {
    asset::AssetLibrary assets;
    auto* meshAsset = assets.create<asset::MeshAsset>("SharedMesh");
    meshAsset->addPrimitive(makeSurfaceMesh());

    scene::Scene sourceScene;
    std::vector<scene::EntityId> entities;
    for (size_t index = 0; index < 128u; ++index) {
        const scene::EntityId entity = sourceScene.createEntity("Entity");
        ASSERT_TRUE(sourceScene.setGeometry(entity, meshAsset->id()));
        entities.push_back(entity);
    }

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.sceneWorld);
    ASSERT_EQ(first.sceneWorld->objects().size(), entities.size());

    std::unordered_map<uint32_t, engine::RenderObjectId> stableIds;
    for (const auto& object : first.sceneWorld->objects()) {
        stableIds.emplace(object.desc.pickId.value, object.id);
    }
    const math::Point3 originalFirstPosition =
            math::Point3::origin().transformedBy(first.sceneWorld->objects().front().desc.worldTransform);

    ASSERT_TRUE(sourceScene.setWorldTransform(entities[73], math::Mat4::translate(math::Vec3(9.0, 2.0, 0.0))));
    renderScene.sync(sourceScene, assets);
    const RenderSubmission patched = builder.build(view);
    ASSERT_TRUE(patched.sceneWorld);
    EXPECT_FALSE(builder.lastStats().fullRebuild);
    EXPECT_EQ(builder.lastStats().patchedObjectCount, 1u);
    EXPECT_EQ(builder.lastStats().updatedObjectCount, 1u);
    EXPECT_EQ(patched.sceneWorld->objects().size(), entities.size());
    for (const auto& object : patched.sceneWorld->objects()) {
        EXPECT_EQ(object.id, stableIds.at(object.desc.pickId.value));
    }
    // 新快照由 COW 存储生成，旧快照必须继续保持原值。
    EXPECT_EQ(math::Point3::origin().transformedBy(first.sceneWorld->objects().front().desc.worldTransform),
              originalFirstPosition);
}

TEST(RenderSubmissionBuilderTests, RemoveAndAddKeepUnrelatedObjectIdsStable) {
    asset::AssetLibrary assets;
    auto* meshAsset = assets.create<asset::MeshAsset>("SharedMesh");
    meshAsset->addPrimitive(makeSurfaceMesh());
    scene::Scene sourceScene;
    const scene::EntityId firstEntity = sourceScene.createEntity("First");
    const scene::EntityId removedEntity = sourceScene.createEntity("Removed");
    ASSERT_TRUE(sourceScene.setGeometry(firstEntity, meshAsset->id()));
    ASSERT_TRUE(sourceScene.setGeometry(removedEntity, meshAsset->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    const engine::PickId firstPick = renderScene.proxy(firstEntity)->pickId;
    const auto firstObjects = first.sceneWorld->objects();
    const auto firstRecord =
            std::ranges::find_if(firstObjects, [&](const auto& object) { return object.desc.pickId == firstPick; });
    ASSERT_NE(firstRecord, firstObjects.end());
    const engine::RenderObjectId stableId = firstRecord->id;

    sourceScene.destroyEntity(removedEntity);
    const scene::EntityId addedEntity = sourceScene.createEntity("Added");
    ASSERT_TRUE(sourceScene.setGeometry(addedEntity, meshAsset->id()));
    renderScene.sync(sourceScene, assets);
    const RenderSubmission patched = builder.build(view);
    ASSERT_TRUE(patched.sceneWorld);
    EXPECT_EQ(builder.lastStats().patchedObjectCount, 2u);
    EXPECT_EQ(builder.lastStats().removedObjectCount, 1u);
    EXPECT_EQ(builder.lastStats().addedObjectCount, 1u);
    const auto patchedObjects = patched.sceneWorld->objects();
    const auto stableRecord =
            std::ranges::find_if(patchedObjects, [&](const auto& object) { return object.desc.pickId == firstPick; });
    ASSERT_NE(stableRecord, patchedObjects.end());
    EXPECT_EQ(stableRecord->id, stableId);
}

TEST(RenderSubmissionBuilderTests, VisibilityUsesAnObjectPatchWithoutChangingIdentity) {
    asset::AssetLibrary assets;
    auto* meshAsset = assets.create<asset::MeshAsset>("Mesh");
    meshAsset->addPrimitive(makeSurfaceMesh());
    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("Entity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, meshAsset->id()));
    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    const ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_EQ(first.sceneWorld->objects().size(), 1u);
    const engine::RenderObjectId stableId = first.sceneWorld->objects().front().id;

    ASSERT_TRUE(sourceScene.setVisible(entity, false));
    renderScene.sync(sourceScene, assets);
    const RenderSubmission hidden = builder.build(view);
    ASSERT_EQ(hidden.sceneWorld->objects().size(), 1u);
    EXPECT_EQ(hidden.sceneWorld->objects().front().id, stableId);
    EXPECT_FALSE(hidden.sceneWorld->objects().front().desc.visible);
    EXPECT_EQ(builder.lastStats().updatedObjectCount, 1u);
    EXPECT_EQ(builder.lastStats().removedObjectCount, 0u);
}

TEST(RenderSubmissionBuilderTests, IncrementalProjectionMatchesFreshFullProjection) {
    asset::AssetLibrary assets;
    auto* meshAsset = assets.create<asset::MeshAsset>("Mesh");
    meshAsset->addPrimitive(makeSurfaceMesh());
    scene::Scene sourceScene;
    const scene::EntityId firstEntity = sourceScene.createEntity("First");
    const scene::EntityId secondEntity = sourceScene.createEntity("Second");
    ASSERT_TRUE(sourceScene.setGeometry(firstEntity, meshAsset->id()));
    ASSERT_TRUE(sourceScene.setGeometry(secondEntity, meshAsset->id()));
    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder incrementalBuilder;
    incrementalBuilder.setScene(&renderScene, &assets);
    const ViewState view;
    ASSERT_TRUE(incrementalBuilder.build(view).sceneWorld);

    ASSERT_TRUE(sourceScene.setWorldTransform(secondEntity, math::Mat4::translate(math::Vec3(4.0, 5.0, 0.0))));
    renderScene.sync(sourceScene, assets);
    const RenderSubmission incremental = incrementalBuilder.build(view);
    RenderSubmissionBuilder fullBuilder;
    fullBuilder.setScene(&renderScene, &assets);
    const RenderSubmission full = fullBuilder.build(view);
    ASSERT_TRUE(incremental.sceneWorld);
    ASSERT_TRUE(full.sceneWorld);
    ASSERT_EQ(incremental.sceneWorld->objects().size(), full.sceneWorld->objects().size());
    const auto fullObjects = full.sceneWorld->objects();
    for (const auto& incrementalObject : incremental.sceneWorld->objects()) {
        const auto fullObject = std::ranges::find_if(fullObjects, [&](const auto& candidate) {
            return candidate.desc.pickId == incrementalObject.desc.pickId;
        });
        ASSERT_NE(fullObject, fullObjects.end());
        EXPECT_EQ(math::Point3::origin().transformedBy(incrementalObject.desc.worldTransform),
                  math::Point3::origin().transformedBy(fullObject->desc.worldTransform));
        ASSERT_EQ(incrementalObject.desc.drawables.size(), fullObject->desc.drawables.size());
        for (size_t drawable = 0; drawable < incrementalObject.desc.drawables.size(); ++drawable) {
            const auto* incrementalGeometry =
                    incremental.sceneWorld->geometry(incrementalObject.desc.drawables[drawable].geometry);
            const auto* fullGeometry = full.sceneWorld->geometry(fullObject->desc.drawables[drawable].geometry);
            ASSERT_NE(incrementalGeometry, nullptr);
            ASSERT_NE(fullGeometry, nullptr);
            EXPECT_EQ(incrementalGeometry->desc.resourceKey, fullGeometry->desc.resourceKey);
        }
    }
}

TEST(RenderSubmissionBuilderTests, ProjectionJournalOverflowRecoversWithFullResync) {
    asset::AssetLibrary assets;
    auto* meshAsset = assets.create<asset::MeshAsset>("Mesh");
    meshAsset->addPrimitive(makeSurfaceMesh());
    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("Entity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, meshAsset->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);
    const ViewState view;
    ASSERT_TRUE(builder.build(view).sceneWorld);

    for (size_t revision = 0; revision < 4100u; ++revision) {
        ASSERT_TRUE(sourceScene.setWorldTransform(
                entity, math::Mat4::translate(math::Vec3(static_cast<double>(revision + 1u), 0.0, 0.0))));
        renderScene.sync(sourceScene, assets);
    }
    const RenderSubmission recovered = builder.build(view);
    ASSERT_TRUE(recovered.sceneWorld);
    EXPECT_TRUE(builder.lastStats().fullRebuild);
    EXPECT_EQ(builder.lastStats().patchedObjectCount, 1u);
    ASSERT_EQ(recovered.sceneWorld->objects().size(), 1u);
    EXPECT_EQ(math::Point3::origin().transformedBy(recovered.sceneWorld->objects().front().desc.worldTransform),
              math::Point3(4100.0, 0.0, 0.0));
}

TEST(RenderSubmissionBuilderTests, SelectionChangesOnlyViewVisualState) {
    asset::AssetLibrary assets;
    auto* meshAsset = assets.create<asset::MeshAsset>("SelectionStableMesh");
    meshAsset->addPrimitive(makeSurfaceMesh());

    scene::Scene sourceScene;
    const scene::EntityId entity = sourceScene.createEntity("SelectionStableEntity");
    ASSERT_TRUE(sourceScene.setGeometry(entity, meshAsset->id()));

    RenderScene renderScene;
    renderScene.sync(sourceScene, assets);
    RenderSubmissionBuilder builder;
    builder.setScene(&renderScene, &assets);

    ViewState view;
    const RenderSubmission first = builder.build(view);
    ASSERT_TRUE(first.sceneWorld);

    ASSERT_TRUE(sourceScene.setSelected(entity, true));
    renderScene.sync(sourceScene, assets);
    const RenderSubmission selected = builder.build(view);

    EXPECT_EQ(selected.sceneWorld, first.sceneWorld);
}

TEST(RenderWorldSnapshotTests, PublishedSnapshotRemainsImmutableAcrossSparseUpdates) {
    engine::RenderWorld world;
    engine::RenderObjectDesc firstDesc;
    firstDesc.visible = true;
    firstDesc.worldBounds = math::AABB3{ math::Point3{ -1.0, -1.0, -1.0 }, math::Point3{ 1.0, 1.0, 1.0 } };
    const engine::RenderObjectId first = world.addObject(firstDesc);
    engine::RenderObjectDesc removedDesc;
    removedDesc.visible = true;
    removedDesc.worldBounds = math::AABB3{ math::Point3{ 10.0, 10.0, 10.0 }, math::Point3{ 11.0, 11.0, 11.0 } };
    const engine::RenderObjectId removed = world.addObject(removedDesc);
    const engine::RenderWorldSnapshot published = world.snapshot();

    firstDesc.worldTransform = math::Mat4::translate(math::Vec3{ 4.0, 0.0, 0.0 });
    firstDesc.worldBounds = math::AABB3{ math::Point3{ 3.0, -1.0, -1.0 }, math::Point3{ 5.0, 1.0, 1.0 } };
    ASSERT_TRUE(world.updateObject(first, firstDesc));
    ASSERT_TRUE(world.removeObject(removed));
    const engine::RenderObjectId added = world.addObject({});
    const engine::RenderWorldSnapshot current = world.snapshot();

    ASSERT_EQ(published.objects().size(), 2u);
    EXPECT_EQ(published.objects().front().id, first);
    auto publishedSecond = published.objects().begin();
    ++publishedSecond;
    EXPECT_EQ(publishedSecond->desc.worldBounds.max.x, 11.0);
    ASSERT_EQ(current.objects().size(), 2u);
    const auto currentObjects = current.objects();
    const auto currentFirst =
            std::ranges::find_if(currentObjects, [&](const auto& record) { return record.id == first; });
    ASSERT_NE(currentFirst, currentObjects.end());
    const math::Point3 expectedPosition{ 4.0, 0.0, 0.0 };
    EXPECT_EQ(math::Point3::origin().transformedBy(currentFirst->desc.worldTransform), expectedPosition);
    EXPECT_NE(std::ranges::find_if(currentObjects, [&](const auto& record) { return record.id == added; }),
              currentObjects.end());
    EXPECT_EQ(current.objects().front().desc.worldBounds.max.x, 5.0);
}

TEST(RenderWorldSnapshotTests, WorldVersionsAreUniqueAndAdvanceOnlyForSuccessfulMutations) {
    engine::RenderWorld world;
    engine::RenderWorld otherWorld;
    const engine::RenderWorldVersion initial = world.snapshot().version();
    const engine::RenderWorldVersion otherInitial = otherWorld.snapshot().version();
    EXPECT_NE(initial.world, 0u);
    EXPECT_NE(initial.world, otherInitial.world);
    EXPECT_EQ(initial.revision, 0u);

    uint64_t expectedRevision = initial.revision;
    const engine::GeometryHandle geometry = world.addGeometry({});
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);
    const engine::RenderMaterialHandle material = world.addMaterial({});
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);
    engine::RenderObjectDesc desc;
    const engine::RenderObjectId object = world.addObject(desc);
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);

    const engine::GeometryHandle staleGeometry{ .index = geometry.index, .generation = geometry.generation + 1u };
    const engine::RenderMaterialHandle staleMaterial{ .index = material.index, .generation = material.generation + 1u };
    const engine::RenderObjectId staleObject{ .index = object.index, .generation = object.generation + 1u };
    const engine::RenderWorldVersion beforeFailedUpdate = world.snapshot().version();
    EXPECT_FALSE(world.updateGeometry(staleGeometry, {}));
    EXPECT_FALSE(world.updateMaterial(staleMaterial, {}));
    EXPECT_FALSE(world.updateObject(staleObject, desc));
    EXPECT_EQ(world.snapshot().version(), beforeFailedUpdate);

    ASSERT_TRUE(world.updateGeometry(geometry, {}));
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);
    ASSERT_TRUE(world.updateMaterial(material, {}));
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);
    desc.visible = false;
    ASSERT_TRUE(world.updateObject(object, desc));
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);

    const engine::RenderWorldVersion beforeFailedRemove = world.snapshot().version();
    EXPECT_FALSE(world.removeGeometry(staleGeometry));
    EXPECT_FALSE(world.removeMaterial(staleMaterial));
    EXPECT_FALSE(world.removeObject(staleObject));
    EXPECT_EQ(world.snapshot().version(), beforeFailedRemove);
    ASSERT_TRUE(world.removeGeometry(geometry));
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);
    ASSERT_TRUE(world.removeMaterial(material));
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);
    ASSERT_TRUE(world.removeObject(object));
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);

    const engine::RenderWorldVersion beforeClear = world.snapshot().version();
    world.clear();
    EXPECT_EQ(world.snapshot().version().revision, ++expectedRevision);
    EXPECT_EQ(expectedRevision, beforeClear.revision + 1u);
}

TEST(RenderWorldSnapshotTests, PublishedSnapshotRemainsStableAcrossWorldMutations) {
    engine::RenderWorld world;
    engine::RenderGeometryDesc geometryDesc;
    const engine::GeometryHandle updatedGeometry = world.addGeometry(geometryDesc);
    const engine::RenderMaterialHandle removedMaterial = world.addMaterial({});
    engine::RenderObjectDesc objectDesc;
    const engine::RenderObjectId updatedObject = world.addObject(objectDesc);
    const engine::RenderObjectId removedObject = world.addObject({});
    const engine::RenderWorldSnapshot previous = world.snapshot();

    geometryDesc.empty = false;
    ASSERT_TRUE(world.updateGeometry(updatedGeometry, geometryDesc));
    const engine::GeometryHandle addedGeometry = world.addGeometry({});
    ASSERT_TRUE(world.removeMaterial(removedMaterial));
    const engine::RenderMaterialHandle addedMaterial = world.addMaterial({});
    objectDesc.visible = false;
    ASSERT_TRUE(world.updateObject(updatedObject, objectDesc));
    ASSERT_TRUE(world.removeObject(removedObject));
    const engine::RenderObjectId addedObject = world.addObject({});
    const engine::RenderWorldSnapshot current = world.snapshot();

    ASSERT_NE(previous.object(updatedObject), nullptr);
    EXPECT_TRUE(previous.object(updatedObject)->desc.visible);
    EXPECT_NE(previous.object(removedObject), nullptr);
    EXPECT_NE(previous.material(removedMaterial), nullptr);
    EXPECT_TRUE(previous.geometry(updatedGeometry)->desc.empty);
    EXPECT_EQ(previous.geometry(addedGeometry), nullptr);
    EXPECT_EQ(previous.material(addedMaterial), nullptr);
    EXPECT_EQ(previous.object(addedObject), nullptr);

    ASSERT_NE(current.object(updatedObject), nullptr);
    EXPECT_FALSE(current.object(updatedObject)->desc.visible);
    EXPECT_FALSE(current.geometry(updatedGeometry)->desc.empty);
    EXPECT_NE(current.geometry(addedGeometry), nullptr);
    EXPECT_EQ(current.material(removedMaterial), nullptr);
    EXPECT_NE(current.material(addedMaterial), nullptr);
    EXPECT_EQ(current.object(removedObject), nullptr);
    EXPECT_NE(current.object(addedObject), nullptr);
    const engine::RenderObjectId staleObject{ .index = addedObject.index, .generation = addedObject.generation + 1u };
    EXPECT_EQ(current.object(staleObject), nullptr);
}

}  // namespace
}  // namespace mulan::view
