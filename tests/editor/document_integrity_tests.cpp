/**
 * @file document_integrity_tests.cpp
 * @brief 文档实体、共享几何资产与命令历史事务边界测试。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <gtest/gtest.h>

#include <mulan/asset/asset_library.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/asset/curve_asset.h>
#include "core/operation/document_operation.h"
#include "core/operation/document_operation_executor.h"
#include "document/document_render_binding.h"

#include <mulan/editor/document/document_session.h>
#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/scene.h>
#include <mulan/view/core/view_context.h>

#include <memory>
#include <string>
#include <variant>

namespace {

using mulan::asset::AssetId;
using mulan::asset::CurveAsset;
using mulan::asset::CurvePrimitive;
using mulan::editor::DocumentOperation;
using mulan::editor::DocumentOperationExecutor;
using mulan::io::Document;
using mulan::io::DocumentEditor;
using mulan::math::Mat4;
using mulan::math::Point3;
using mulan::math::Segment3;
using mulan::scene::EntityId;

CurvePrimitive segment(double endX) {
    return CurvePrimitive::segment(Segment3{ Point3{ 0.0, 0.0, 0.0 }, Point3{ endX, 0.0, 0.0 } });
}

TEST(DocumentSessionPolicy, DirtyDraftDoesNotRequestDiscardConfirmation) {
    auto document = std::make_unique<Document>("unsaved-draft");
    document->markDirty();
    DocumentSession session(std::move(document));

    EXPECT_EQ(session.kind(), DocumentSessionKind::Draft);
    EXPECT_FALSE(session.requiresDiscardConfirmation());
}

double segmentEndX(const CurveAsset& curve) {
    const auto& primitive = std::get<mulan::asset::CurveSegmentPrimitive>(curve.elements().front().primitive.data());
    return primitive.segment.end.x;
}

class TestShapeStorage final : public mulan::modeling::ShapeStorage {
public:
    explicit TestShapeStorage(double marker) : marker_(marker) {}

    mulan::modeling::BodyKind bodyKind() const override { return mulan::modeling::BodyKind::Solid; }

    mulan::math::AABB3 bounds() const override {
        return { Point3{ marker_, 0.0, 0.0 }, Point3{ marker_ + 1.0, 1.0, 1.0 } };
    }

    mulan::core::Result<mulan::modeling::TessellatedGeometry> tessellate(
            const mulan::modeling::TessellationOptions&) const override {
        return mulan::modeling::TessellatedGeometry{};
    }

private:
    double marker_ = 0.0;
};

mulan::modeling::Shape testShape(double marker) {
    return mulan::modeling::makeShapeFromStorage(std::make_shared<TestShapeStorage>(marker));
}

class TestShapeOps final : public mulan::modeling::IShapeOps {
public:
    mulan::core::Result<mulan::modeling::Shape> extrude(const mulan::modeling::ExtrudeParams&) override {
        return testShape(50.0);
    }

    mulan::core::Result<mulan::modeling::Shape> boolean(const mulan::modeling::Shape&, const mulan::modeling::Shape&,
                                                        mulan::modeling::BooleanOp) override {
        return testShape(100.0);
    }
};

class ShapeBackendSelectionGuard {
public:
    ShapeBackendSelectionGuard()
        : registry_(mulan::modeling::ShapeOpsRegistry::instance()), previous_(registry_.selectedBackend()) {
        registry_.registerOps("document-integrity-test", std::make_unique<TestShapeOps>());
        registry_.selectBackend("document-integrity-test");
    }

    ~ShapeBackendSelectionGuard() { registry_.selectBackend(previous_); }

private:
    mulan::modeling::ShapeOpsRegistry& registry_;
    std::string previous_;
};

}  // namespace

TEST(DocumentAssetLifetime, DeletingOneSharedInstanceKeepsAssetAlive) {
    Document document("shared-geometry");
    DocumentEditor editor(document);

    const auto created = editor.createCurve("Line", segment(1.0));
    ASSERT_TRUE(created);
    const AssetId sharedGeometry = editor.geometryAssetForEntity(created.entity);
    ASSERT_TRUE(sharedGeometry);

    const EntityId copy = editor.copyEntityWithTransform(created.entity, Mat4::identity());
    ASSERT_TRUE(copy);
    ASSERT_EQ(editor.geometryAssetForEntity(copy), sharedGeometry);
    ASSERT_EQ(editor.geometryReferenceCount(sharedGeometry), 2u);

    EXPECT_TRUE(editor.removeEntity(created.entity, true));
    EXPECT_TRUE(document.scene()->isValid(copy));
    EXPECT_TRUE(document.assets()->contains(sharedGeometry));
    EXPECT_EQ(editor.geometryReferenceCount(sharedGeometry), 1u);

    EXPECT_TRUE(editor.removeEntity(copy, true));
    EXPECT_FALSE(document.assets()->contains(sharedGeometry));
}

TEST(DocumentAssetLifetime, ReferencedGeometryCannotBeRemovedDirectly) {
    Document document("referenced-geometry");
    DocumentEditor editor(document);
    const auto created = editor.createCurve("Line", segment(1.0));
    ASSERT_TRUE(created);

    const AssetId geometry = editor.geometryAssetForEntity(created.entity);
    ASSERT_TRUE(geometry);
    EXPECT_FALSE(editor.removeGeometryAsset(geometry));
    EXPECT_TRUE(document.assets()->contains(geometry));
    EXPECT_TRUE(document.scene()->isValid(created.entity));
}

TEST(DocumentCopyOnWrite, UpdatingSharedCurveDoesNotMutateSibling) {
    Document document("curve-cow");
    DocumentEditor editor(document);

    const auto created = editor.createCurve("Line", segment(1.0));
    ASSERT_TRUE(created);
    const EntityId sibling = editor.copyEntityWithTransform(created.entity, Mat4::identity());
    ASSERT_TRUE(sibling);

    const AssetId originalGeometry = editor.geometryAssetForEntity(created.entity);
    ASSERT_EQ(editor.geometryAssetForEntity(sibling), originalGeometry);
    ASSERT_TRUE(editor.updateCurve(created.entity, created.element, segment(5.0)));

    const AssetId editedGeometry = editor.geometryAssetForEntity(created.entity);
    EXPECT_NE(editedGeometry, originalGeometry);
    EXPECT_EQ(editor.geometryAssetForEntity(sibling), originalGeometry);

    const auto* original = dynamic_cast<const CurveAsset*>(document.assets()->asset(originalGeometry));
    const auto* edited = dynamic_cast<const CurveAsset*>(document.assets()->asset(editedGeometry));
    ASSERT_NE(original, nullptr);
    ASSERT_NE(edited, nullptr);
    EXPECT_DOUBLE_EQ(segmentEndX(*original), 1.0);
    EXPECT_DOUBLE_EQ(segmentEndX(*edited), 5.0);
}

TEST(DocumentCopyOnWrite, BooleanDoesNotMutateSharedTargetGeometry) {
    ShapeBackendSelectionGuard backend;
    Document document("boolean-cow");
    DocumentEditor editor(document);

    const EntityId target = editor.createBody("Target", testShape(1.0));
    ASSERT_TRUE(target);
    const EntityId sibling = editor.copyEntityWithTransform(target, Mat4::identity());
    ASSERT_TRUE(sibling);
    const EntityId tool = editor.createBody("Tool", testShape(2.0));
    ASSERT_TRUE(tool);

    const AssetId originalGeometry = editor.geometryAssetForEntity(target);
    ASSERT_EQ(editor.geometryAssetForEntity(sibling), originalGeometry);
    ASSERT_TRUE(editor.booleanSubtract(target, tool, mulan::modeling::BooleanOp::Difference));

    const AssetId resultGeometry = editor.geometryAssetForEntity(target);
    EXPECT_NE(resultGeometry, originalGeometry);
    EXPECT_EQ(editor.geometryAssetForEntity(sibling), originalGeometry);
    EXPECT_TRUE(document.assets()->contains(originalGeometry));
    EXPECT_FALSE(document.scene()->isValid(tool));

    const auto* original = dynamic_cast<const mulan::asset::BRepAsset*>(document.assets()->asset(originalGeometry));
    const auto* result = dynamic_cast<const mulan::asset::BRepAsset*>(document.assets()->asset(resultGeometry));
    ASSERT_NE(original, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_DOUBLE_EQ(original->shape().bounds().min.x, 1.0);
    EXPECT_DOUBLE_EQ(result->shape().bounds().min.x, 100.0);
}

TEST(DocumentOperationTransactions, IrreversibleChangeInvalidatesBothHistoryBranches) {
    auto document = std::make_unique<Document>("history-boundary");
    Document* documentPtr = document.get();
    DocumentSession session(std::move(document));
    DocumentOperationExecutor executor;
    executor.bind(&session, nullptr);

    ASSERT_TRUE(executor.execute(DocumentOperation::createCurve("Recorded", segment(1.0))));
    ASSERT_TRUE(executor.canUndo());
    ASSERT_TRUE(executor.undo());
    ASSERT_TRUE(executor.canRedo());

    DocumentEditor editor(*documentPtr);
    const auto independent = editor.createCurve("Independent", segment(2.0));
    ASSERT_TRUE(independent);
    ASSERT_TRUE(executor.execute(DocumentOperation::removeEntities({ independent.entity }, true)));

    EXPECT_FALSE(executor.canUndo());
    EXPECT_FALSE(executor.canRedo());
}

TEST(DocumentOperationHistoryOwnership, SurvivesUnbindRebindAndExecutorReplacement) {
    auto document = std::make_unique<Document>("session-owned-history");
    Document* documentPtr = document.get();
    DocumentSession session(std::move(document));

    DocumentOperationExecutor firstExecutor;
    firstExecutor.bind(&session, nullptr);
    ASSERT_TRUE(firstExecutor.execute(DocumentOperation::createCurve("Recorded", segment(1.0))));
    ASSERT_EQ(documentPtr->scene()->entityCount(), 1u);
    ASSERT_TRUE(firstExecutor.canUndo());

    firstExecutor.unbind();
    EXPECT_FALSE(firstExecutor.isBound());
    EXPECT_FALSE(firstExecutor.canUndo());
    EXPECT_FALSE(firstExecutor.canRedo());
    EXPECT_FALSE(firstExecutor.undo());
    EXPECT_EQ(documentPtr->scene()->entityCount(), 1u);

    firstExecutor.bind(&session, nullptr);
    ASSERT_TRUE(firstExecutor.canUndo());
    ASSERT_TRUE(firstExecutor.undo());
    EXPECT_EQ(documentPtr->scene()->entityCount(), 0u);
    ASSERT_TRUE(firstExecutor.canRedo());

    // executor 不拥有历史；换一个执行器后仍能继续同一会话的 redo 分支。
    firstExecutor.unbind();
    DocumentOperationExecutor replacementExecutor;
    replacementExecutor.bind(&session, nullptr);
    ASSERT_TRUE(replacementExecutor.canRedo());
    ASSERT_TRUE(replacementExecutor.redo());
    EXPECT_EQ(documentPtr->scene()->entityCount(), 1u);
    EXPECT_TRUE(replacementExecutor.canUndo());
}

TEST(DocumentOperationHistoryOwnership, DifferentDocumentSessionsRemainIsolated) {
    auto firstDocument = std::make_unique<Document>("first-history");
    Document* firstDocumentPtr = firstDocument.get();
    DocumentSession firstSession(std::move(firstDocument));

    auto secondDocument = std::make_unique<Document>("second-history");
    Document* secondDocumentPtr = secondDocument.get();
    DocumentSession secondSession(std::move(secondDocument));

    DocumentOperationExecutor executor;
    executor.bind(&firstSession, nullptr);
    ASSERT_TRUE(executor.execute(DocumentOperation::createCurve("First", segment(1.0))));
    ASSERT_TRUE(executor.canUndo());
    ASSERT_EQ(firstDocumentPtr->scene()->entityCount(), 1u);

    // 直接换绑定不清空原会话，也不得把原会话的历史带入新会话。
    executor.bind(&secondSession, nullptr);
    EXPECT_FALSE(executor.canUndo());
    ASSERT_TRUE(executor.execute(DocumentOperation::createCurve("Second", segment(2.0))));
    ASSERT_TRUE(executor.canUndo());
    ASSERT_EQ(secondDocumentPtr->scene()->entityCount(), 1u);

    executor.bind(&firstSession, nullptr);
    ASSERT_TRUE(executor.canUndo());
    ASSERT_TRUE(executor.undo());
    EXPECT_EQ(firstDocumentPtr->scene()->entityCount(), 0u);
    EXPECT_EQ(secondDocumentPtr->scene()->entityCount(), 1u);

    executor.bind(&secondSession, nullptr);
    ASSERT_TRUE(executor.canUndo());
    ASSERT_TRUE(executor.undo());
    EXPECT_EQ(secondDocumentPtr->scene()->entityCount(), 0u);
    EXPECT_FALSE(executor.canUndo());
}

TEST(DocumentOperationTransactions, InvalidBatchRemovalDoesNotPartiallyCommit) {
    auto document = std::make_unique<Document>("batch-removal");
    Document* documentPtr = document.get();
    DocumentEditor editor(*documentPtr);
    const auto first = editor.createCurve("First", segment(1.0));
    const auto second = editor.createCurve("Second", segment(2.0));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    DocumentSession session(std::move(document));
    DocumentOperationExecutor executor;
    executor.bind(&session, nullptr);

    EXPECT_FALSE(executor.execute(DocumentOperation::removeEntities({ first.entity, EntityId::invalid() }, true)));
    EXPECT_TRUE(documentPtr->scene()->isValid(first.entity));
    EXPECT_TRUE(documentPtr->scene()->isValid(second.entity));
    EXPECT_EQ(documentPtr->scene()->entityCount(), 2u);
}

TEST(DocumentRenderInvalidation, BindingReportsFrameDemandThroughSingleCallback) {
    auto document = std::make_unique<Document>("render-invalidation");
    DocumentEditor editor(*document);
    ASSERT_TRUE(editor.createCurve("Line", segment(1.0)));

    DocumentSession session(std::move(document));
    mulan::view::ViewContext view;
    mulan::editor::DocumentRenderBinding binding;
    size_t invalidationCount = 0;
    binding.setFrameInvalidationCallback([&invalidationCount]() { ++invalidationCount; });
    binding.bind(session, view);

    binding.refresh();
    EXPECT_EQ(invalidationCount, 1u);

    // 仅更新裁剪面不会自行提交新帧；由实际输入结果决定是否失效。
    binding.prepareFrame();
    EXPECT_EQ(invalidationCount, 1u);

    binding.fitAll();
    EXPECT_EQ(invalidationCount, 2u);

    binding.unbind();
    binding.refresh();
    binding.prepareFrame();
    EXPECT_EQ(invalidationCount, 2u);
}

TEST(DocumentCameraClipPlanes, CommittedOffAxisSmallCircleKeepsTheCurrentCompositionVisible) {
    auto document = std::make_unique<Document>("off-axis-small-circle");
    Document* documentPtr = document.get();
    DocumentSession session(std::move(document));
    mulan::view::ViewContext view;
    mulan::editor::DocumentRenderBinding binding;
    binding.bind(session, view);

    auto& camera = view.camera();
    camera.setTarget({ 0.0, 0.0, 0.0 });
    camera.setDistance(10.0);
    camera.setOrthoSize(5.0);
    camera.setClipPlanes(0.1, 1000.0);
    const auto initialTarget = camera.target();
    const double initialDistance = camera.distance();
    const double initialOrthoSize = camera.orthoSize();

    DocumentEditor editor(*documentPtr);
    ASSERT_TRUE(editor.createCurve("SmallCircle",
                                   CurvePrimitive::circle(mulan::math::Circle3{ Point3{ 5.0, 0.0, 0.0 }, 0.01 })));
    binding.refresh();

    const auto* renderScene = binding.renderScene();
    ASSERT_NE(renderScene, nullptr);
    const auto& sphere = renderScene->sceneBoundsSphere();
    ASSERT_TRUE(sphere.isValid());
    const double depth = (sphere.center.asVec() - camera.eyePosition()).dot(camera.forward());
    EXPECT_LT(camera.nearPlane(), depth - sphere.radius);
    EXPECT_GT(camera.farPlane(), depth + sphere.radius);
    EXPECT_EQ(camera.target(), initialTarget);
    EXPECT_DOUBLE_EQ(camera.distance(), initialDistance);
    EXPECT_DOUBLE_EQ(camera.orthoSize(), initialOrthoSize);

    ASSERT_TRUE(editor.createCurve("FollowingLine", segment(1.0)));
    binding.refresh();
    binding.prepareFrame();
    const auto& expandedSphere = renderScene->sceneBoundsSphere();
    const double expandedDepth = (expandedSphere.center.asVec() - camera.eyePosition()).dot(camera.forward());
    EXPECT_LT(camera.nearPlane(), expandedDepth - expandedSphere.radius);
    EXPECT_GT(camera.farPlane(), expandedDepth + expandedSphere.radius);
}

TEST(DocumentCameraLifecycle, EmptyDocumentBindResetsThePreviousComposition) {
    mulan::view::ViewContext view;
    view.camera().setOrthographic(false);
    view.camera().setTarget({ 12.0, -4.0, 8.0 });
    view.camera().setDistance(42.0);
    view.camera().setClipPlanes(3.0, 90.0);
    view.camera().pan(100.0, -80.0);

    auto document = std::make_unique<Document>("empty-camera-reset");
    DocumentSession session(std::move(document));
    mulan::editor::DocumentRenderBinding binding;
    binding.bind(session, view);

    EXPECT_TRUE(view.camera().isOrthographic());
    EXPECT_EQ(view.camera().target(), mulan::math::Vec3(0.0, 0.0, 0.0));
    EXPECT_DOUBLE_EQ(view.camera().distance(), 10.0);
    EXPECT_DOUBLE_EQ(view.camera().orthoSize(), 5.0);
    EXPECT_DOUBLE_EQ(view.camera().nearPlane(), 0.1);
    EXPECT_DOUBLE_EQ(view.camera().farPlane(), 1000.0);
}

TEST(DocumentCameraClipPlanes, InteractiveRangeTightensOnlyAfterTheInteractionSettles) {
    auto document = std::make_unique<Document>("interactive-clip-policy");
    DocumentEditor editor(*document);
    ASSERT_TRUE(editor.createCurve("Line", segment(1.0)));

    DocumentSession session(std::move(document));
    mulan::view::ViewContext view;
    mulan::editor::DocumentRenderBinding binding;
    binding.bind(session, view);
    view.camera().setClipPlanes(0.1, 1000.0);
    view.camera().setTarget({ 0.0, 0.0, 0.0 });

    binding.prepareFrame(mulan::editor::ClipUpdateMode::Interactive);
    EXPECT_DOUBLE_EQ(view.camera().nearPlane(), 0.1);
    EXPECT_DOUBLE_EQ(view.camera().farPlane(), 1000.0);

    // 即使场景和相机版本未再次变化，结束交互也必须消费待收紧状态。
    binding.prepareFrame(mulan::editor::ClipUpdateMode::Settled);
    EXPECT_GT(view.camera().nearPlane(), 0.1);
    EXPECT_LT(view.camera().farPlane(), 1000.0);
}

TEST(DocumentCameraClipPlanes, PreviewAndCommittedLargeCircleStayInFrontDuringOrbit) {
    auto document = std::make_unique<Document>("large-circle-orbit");
    Document* documentPtr = document.get();
    DocumentSession session(std::move(document));
    mulan::view::ViewContext view;
    mulan::editor::DocumentRenderBinding binding;
    binding.bind(session, view);

    auto& camera = view.camera();
    camera.setOrthoSize(150.0);
    camera.setDistance(10.0);
    const auto circle = CurvePrimitive::circle(mulan::math::Circle3{ Point3::origin(), 100.0 });
    view.previewLayer().setCurve(circle);

    binding.prepareFrame(mulan::editor::ClipUpdateMode::Interactive);
    ASSERT_FALSE(view.previewLayer().drawables().empty());
    const auto previewSphere = mulan::math::Sphere3::fromAABB(view.previewLayer().drawables().front().mesh.bounds);
    ASSERT_TRUE(previewSphere.isValid());
    double depth = (previewSphere.center.asVec() - camera.eyePosition()).dot(camera.forward());
    EXPECT_GT(depth - previewSphere.radius, camera.nearPlane());
    EXPECT_GT(camera.farPlane(), depth + previewSphere.radius);

    DocumentEditor editor(*documentPtr);
    ASSERT_TRUE(editor.createCurve("LargeCircle", circle));
    view.previewLayer().clearToolGeometry();
    binding.refresh();
    binding.prepareFrame(mulan::editor::ClipUpdateMode::Settled);

    camera.setRotation(mulan::math::Quat::fromAxisAngle(mulan::math::Vec3::unitY(), mulan::math::kPi * 0.5));
    binding.prepareFrame(mulan::editor::ClipUpdateMode::Interactive);
    const auto& sceneSphere = binding.renderScene()->sceneBoundsSphere();
    ASSERT_TRUE(sceneSphere.isValid());
    depth = (sceneSphere.center.asVec() - camera.eyePosition()).dot(camera.forward());
    EXPECT_GT(depth - sceneSphere.radius, camera.nearPlane());
    EXPECT_GT(camera.farPlane(), depth + sceneSphere.radius);
}
