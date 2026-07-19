/**
 * @file document_editor.h
 * @brief DocumentEditor 提供面向命令的 Document 编辑操作。
 * @author hxxcxx
 * @date 2026-07-07
 *
 * Document 拥有数据容器。DocumentEditor 承担编辑意图，例如根据结构化图元
 * 创建或更新曲线资产。
 */
#pragma once

#include <mulan/asset/curve_asset.h>
#include <mulan/asset/face_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/modeling/core/shape.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/scene/entity_id.h>

#include <cstddef>
#include <string>
#include <vector>

namespace mulan {

class Document;

namespace editor {

struct CurveCreateResult {
    scene::EntityId entity;
    asset::CurveElementId element;

    bool valid() const { return entity && element.valid(); }
    explicit operator bool() const { return valid(); }
};

class DocumentEditor {
public:
    explicit DocumentEditor(Document& document) : document_(document) {}

    CurveCreateResult createCurve(std::string name, asset::CurvePrimitive primitive);
    scene::EntityId createFace(std::string name, asset::FaceDefinition face);
    scene::EntityId createMesh(std::string name, std::vector<asset::MeshPrimitive> primitives);
    /// 拉伸/布尔等建模操作产出的 Shape 落成 BRepAsset 实体。
    scene::EntityId createBody(std::string name, modeling::Shape shape);
    /// 两体布尔:target ⊕ tool。共享 target 先执行 COW，再删除 tool 实体。
    bool booleanSubtract(scene::EntityId target, scene::EntityId tool, modeling::BooleanOp op);
    bool updateCurve(scene::EntityId entity, asset::CurveElementId element, asset::CurvePrimitive primitive);
    bool updateCurveAsset(scene::EntityId entity, asset::AssetId geometry, asset::CurveElementId element,
                          asset::CurvePrimitive primitive);
    bool updateFaceAsset(scene::EntityId entity, asset::AssetId geometry, asset::FaceDefinition face);
    bool updateEntityTransform(scene::EntityId entity, const math::Mat4& worldTransform);
    scene::EntityId copyEntityWithTransform(scene::EntityId source, const math::Mat4& worldTransform);
    bool removeEntity(scene::EntityId entity, bool removeGeometryAsset = true);
    asset::AssetId geometryAssetForEntity(scene::EntityId entity) const;
    size_t geometryReferenceCount(asset::AssetId geometry) const;
    asset::AssetId duplicateGeometryAsset(asset::AssetId geometry, std::string nameSuffix = " Copy");
    bool setEntityGeometry(scene::EntityId entity, asset::AssetId geometry);
    bool removeGeometryAsset(asset::AssetId geometry);

private:
    asset::CurveAsset* curveAssetFor(scene::EntityId entity) const;
    asset::CurveAsset* curveAsset(asset::AssetId geometry) const;
    asset::FaceAsset* faceAsset(asset::AssetId geometry) const;

    Document& document_;
};

}  // namespace editor
}  // namespace mulan
