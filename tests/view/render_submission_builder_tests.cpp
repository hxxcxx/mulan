/**
 * @file render_submission_builder_tests.cpp
 * @brief 验证 GPU 资源批次确认语义与预览资源键的稳定性。
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

#include <cstddef>
#include <vector>

namespace mulan::view {
namespace {

graphics::Mesh makePreviewMesh() {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.topology = graphics::PrimitiveTopology::LineList;
    mesh.vertices.resize(static_cast<size_t>(mesh.layout.stride()) * 2u);
    return mesh;
}

graphics::Mesh makeSurfaceMesh() {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.topology = graphics::PrimitiveTopology::TriangleList;
    mesh.vertices.resize(static_cast<size_t>(mesh.layout.stride()) * 3u);
    mesh.computeBounds();
    return mesh;
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
    preview.setMesh(makePreviewMesh());

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

TEST(RenderSubmissionBuilderTests, InvalidatingGpuDomainRebuildsCurrentResources) {
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

TEST(RenderSubmissionBuilderTests, MaterialLessDrawableKeepsStableDefaultIdentityAcrossWorldRebuilds) {
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
    ASSERT_TRUE(first.world);
    ASSERT_EQ(first.world->materials().size(), 1u);
    const engine::AssetGpuKey stableKey = first.world->materials().front().desc.resourceKey;
    ASSERT_EQ(stableKey, engine::defaultRenderMaterialResourceKey());
    uint32_t previousHandleGeneration = first.world->materials().front().handle.generation;

    bool selected = false;
    for (size_t rebuild = 0; rebuild < 32u; ++rebuild) {
        selected = !selected;
        ASSERT_TRUE(sourceScene.setSelected(entity, selected));
        renderScene.sync(sourceScene, assets);

        const RenderSubmission submission = builder.build(view);
        ASSERT_TRUE(submission.rebuiltWorld);
        ASSERT_TRUE(submission.world);
        ASSERT_EQ(submission.world->materials().size(), 1u);
        const auto& material = submission.world->materials().front();
        EXPECT_EQ(material.desc.resourceKey, stableKey);
        EXPECT_NE(material.handle.generation, previousHandleGeneration);
        previousHandleGeneration = material.handle.generation;
    }
}

}  // namespace
}  // namespace mulan::view
