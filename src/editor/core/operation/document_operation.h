/**
 * @file document_operation.h
 * @brief 定义编辑工具产出的文档操作意图。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "geometry_mutation.h"

#include <mulan/asset/curve_asset.h>
#include <mulan/asset/face_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>
#include <mulan/modeling/core/shape.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/scene/components/light_component.h>
#include <mulan/scene/entity_id.h>

#include <string>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace mulan::editor {

struct CreateCurveOperation {
    std::string name;
    asset::CurvePrimitive primitive;
};

struct CreateMeshOperation {
    std::string name;
    std::vector<asset::MeshPrimitive> primitives;
};

struct ExtrudeFaceOperation {
    std::string name;
    modeling::ExtrudeParams params;
};

struct BooleanOperation {
    scene::EntityId target = scene::EntityId::invalid();
    scene::EntityId tool = scene::EntityId::invalid();
    modeling::BooleanOp op = modeling::BooleanOp::Difference;
};

struct CreateFaceOperation {
    std::string name;
    asset::FaceDefinition face;
};

struct UpdateCurveOperation {
    scene::EntityId entity = scene::EntityId::invalid();
    asset::CurveElementId element = asset::CurveElementId::invalid();
    asset::CurvePrimitive primitive;
};

struct UpdateGeometryOperation {
    GeometryEditRequest request;
};

struct EntityTransformUpdate {
    scene::EntityId entity = scene::EntityId::invalid();
    math::Mat4 worldTransform{ 1.0 };

    bool valid() const { return static_cast<bool>(entity); }
};

struct UpdateEntityTransformsOperation {
    std::vector<EntityTransformUpdate> updates;
};

struct CopyEntityTransformsOperation {
    std::vector<EntityTransformUpdate> updates;
};

struct RemoveEntitiesOperation {
    std::vector<scene::EntityId> entities;
    bool removeGeometryAssets = true;
};

struct CurveAssetSnapshot {
    std::vector<asset::CurveElement> elements;
};

struct FaceAssetSnapshot {
    asset::FaceDefinition face;
};

struct MeshAssetSnapshot {
    std::vector<asset::MeshPrimitive> primitives;
};

struct TessellatedAssetSnapshot {
    graphics::Mesh solid;
    graphics::Mesh wire;
};

struct BRepAssetSnapshot {
    modeling::Shape shape;
};

using GeometryAssetSnapshotData = std::variant<CurveAssetSnapshot, FaceAssetSnapshot, MeshAssetSnapshot,
                                               TessellatedAssetSnapshot, BRepAssetSnapshot>;

struct GeometryAssetSnapshot {
    asset::AssetId originalId = asset::AssetId::invalid();
    std::string name;
    GeometryAssetSnapshotData data;
};

struct EntityStateSnapshot {
    scene::EntityId originalId = scene::EntityId::invalid();
    std::string name;
    math::Mat4 localTransform{ 1.0 };
    scene::EntityId parent = scene::EntityId::invalid();
    std::vector<scene::EntityId> children;
    asset::AssetId geometry = asset::AssetId::invalid();
    bool visible = true;
    bool selected = false;
    std::vector<asset::AssetId> materialSlots;
    std::optional<scene::LightComponent> light;
};

struct RestoreEntitiesOperation {
    std::vector<EntityStateSnapshot> entities;
    std::vector<GeometryAssetSnapshot> geometryAssets;
    bool removeGeometryAssetsOnInverse = true;
    /// Boolean undo reuses the still-live target and restores its mutated BRep in place.
    bool restoreExistingEntities = false;
    bool overwriteExistingAssets = false;
};

struct EntityIdRemap {
    scene::EntityId from;
    scene::EntityId to;
};

struct AssetIdRemap {
    asset::AssetId from;
    asset::AssetId to;
};

using DocumentOperationData =
        std::variant<CreateCurveOperation, CreateFaceOperation, CreateMeshOperation, ExtrudeFaceOperation,
                     BooleanOperation, UpdateCurveOperation, UpdateGeometryOperation, UpdateEntityTransformsOperation,
                     CopyEntityTransformsOperation, RemoveEntitiesOperation, RestoreEntitiesOperation>;

class DocumentOperation {
public:
    static DocumentOperation createCurve(std::string name, asset::CurvePrimitive primitive);
    static DocumentOperation createFace(std::string name, asset::FaceDefinition face);
    static DocumentOperation createMesh(std::string name, std::vector<asset::MeshPrimitive> primitives);
    static DocumentOperation extrudeFace(std::string name, modeling::ExtrudeParams params);
    static DocumentOperation booleanSubtract(scene::EntityId target, scene::EntityId tool, modeling::BooleanOp op);
    static DocumentOperation updateCurve(scene::EntityId entity, asset::CurveElementId element,
                                         asset::CurvePrimitive primitive);
    static DocumentOperation updateGeometry(GeometryEditRequest request);
    static DocumentOperation updateFaceGeometry(scene::EntityId entity, asset::FaceDefinition face);
    static DocumentOperation updateEntityTransforms(std::vector<EntityTransformUpdate> updates);
    static DocumentOperation copyEntityTransforms(std::vector<EntityTransformUpdate> updates);
    static DocumentOperation removeEntities(std::vector<scene::EntityId> entities, bool removeGeometryAssets = true);
    static DocumentOperation restoreEntities(RestoreEntitiesOperation snapshot);

    const DocumentOperationData& data() const { return data_; }
    DocumentOperationData& data() { return data_; }

private:
    explicit DocumentOperation(DocumentOperationData data) : data_(std::move(data)) {}

    DocumentOperationData data_;
};

void remapDocumentOperation(DocumentOperation& operation, std::span<const EntityIdRemap> entityRemaps,
                            std::span<const AssetIdRemap> assetRemaps);

}  // namespace mulan::editor
