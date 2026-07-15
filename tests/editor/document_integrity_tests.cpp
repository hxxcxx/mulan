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
#include <mulan/editor/core/operation/document_operation.h>
#include <mulan/editor/core/operation/document_operation_executor.h>
#include <mulan/editor/document/document_session.h>
#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/scene.h>

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
