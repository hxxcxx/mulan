/**
 * @file document_operation.cpp
 * @brief DocumentOperation 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "document_operation.h"

#include <utility>

namespace mulan::app {

DocumentOperation DocumentOperation::createCurve(std::string name, asset::CurvePrimitive primitive) {
    return DocumentOperation(CreateCurveOperation{ std::move(name), std::move(primitive) });
}

DocumentOperation DocumentOperation::createFace(std::string name, asset::FaceDefinition face) {
    return DocumentOperation(CreateFaceOperation{ std::move(name), std::move(face) });
}

DocumentOperation DocumentOperation::createMesh(std::string name, std::vector<asset::MeshPrimitive> primitives) {
    return DocumentOperation(CreateMeshOperation{ std::move(name), std::move(primitives) });
}

DocumentOperation DocumentOperation::updateCurve(scene::EntityId entity, asset::CurveElementId element,
                                                 asset::CurvePrimitive primitive) {
    return DocumentOperation(UpdateCurveOperation{ entity, element, std::move(primitive) });
}

DocumentOperation DocumentOperation::updateEntityTransforms(std::vector<EntityTransformUpdate> updates) {
    return DocumentOperation(UpdateEntityTransformsOperation{ std::move(updates) });
}

DocumentOperation DocumentOperation::copyEntityTransforms(std::vector<EntityTransformUpdate> updates) {
    return DocumentOperation(CopyEntityTransformsOperation{ std::move(updates) });
}

DocumentOperation DocumentOperation::removeEntities(std::vector<scene::EntityId> entities, bool removeGeometryAssets) {
    return DocumentOperation(RemoveEntitiesOperation{ std::move(entities), removeGeometryAssets });
}

}  // namespace mulan::app
