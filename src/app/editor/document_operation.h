/**
 * @file document_operation.h
 * @brief 定义编辑工具产出的文档操作意图。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include <mulan/asset/curve_asset.h>
#include <mulan/asset/face_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mulan::app {

struct CreateCurveOperation {
    std::string name;
    asset::CurvePrimitive primitive;
};

struct CreateMeshOperation {
    std::string name;
    std::vector<asset::MeshPrimitive> primitives;
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

struct EntityTransformUpdate {
    scene::EntityId entity = scene::EntityId::invalid();
    math::Mat4 worldTransform{ 1.0 };

    bool valid() const { return static_cast<bool>(entity); }
};

struct UpdateEntityTransformsOperation {
    std::vector<EntityTransformUpdate> updates;
};

using DocumentOperationData = std::variant<CreateCurveOperation, CreateFaceOperation, CreateMeshOperation,
                                           UpdateCurveOperation, UpdateEntityTransformsOperation>;

class DocumentOperation {
public:
    static DocumentOperation createCurve(std::string name, asset::CurvePrimitive primitive);
    static DocumentOperation createFace(std::string name, asset::FaceDefinition face);
    static DocumentOperation createMesh(std::string name, std::vector<asset::MeshPrimitive> primitives);
    static DocumentOperation updateCurve(scene::EntityId entity, asset::CurveElementId element,
                                         asset::CurvePrimitive primitive);
    static DocumentOperation updateEntityTransforms(std::vector<EntityTransformUpdate> updates);

    const DocumentOperationData& data() const { return data_; }
    DocumentOperationData& data() { return data_; }

private:
    explicit DocumentOperation(DocumentOperationData data) : data_(std::move(data)) {}

    DocumentOperationData data_;
};

}  // namespace mulan::app
