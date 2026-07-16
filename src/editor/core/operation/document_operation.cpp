/**
 * @file document_operation.cpp
 * @brief DocumentOperation 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "document_operation.h"

#include <utility>

namespace mulan::editor {

DocumentOperation DocumentOperation::createCurve(std::string name, asset::CurvePrimitive primitive) {
    return DocumentOperation(CreateCurveOperation{ std::move(name), std::move(primitive) });
}

DocumentOperation DocumentOperation::createFace(std::string name, asset::FaceDefinition face) {
    return DocumentOperation(CreateFaceOperation{ std::move(name), std::move(face) });
}

DocumentOperation DocumentOperation::createMesh(std::string name, std::vector<asset::MeshPrimitive> primitives) {
    return DocumentOperation(CreateMeshOperation{ std::move(name), std::move(primitives) });
}

DocumentOperation DocumentOperation::extrudeFace(std::string name, modeling::ExtrudeParams params) {
    return DocumentOperation(ExtrudeFaceOperation{ std::move(name), std::move(params) });
}

DocumentOperation DocumentOperation::booleanSubtract(scene::EntityId target, scene::EntityId tool,
                                                     modeling::BooleanOp op) {
    return DocumentOperation(BooleanOperation{ target, tool, op });
}

DocumentOperation DocumentOperation::updateCurve(scene::EntityId entity, asset::CurveElementId element,
                                                 asset::CurvePrimitive primitive) {
    return updateGeometry(GeometryEditRequest{
            .entity = entity,
            .mutation = CurveElementGeometryMutation{ .element = element, .primitive = std::move(primitive) },
    });
}

DocumentOperation DocumentOperation::updateGeometry(GeometryEditRequest request) {
    return DocumentOperation(UpdateGeometryOperation{ std::move(request) });
}

DocumentOperation DocumentOperation::updateFaceGeometry(scene::EntityId entity, asset::FaceDefinition face) {
    return updateGeometry(GeometryEditRequest{
            .entity = entity,
            .mutation = FaceDefinitionGeometryMutation{ .face = std::move(face) },
    });
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

DocumentOperation DocumentOperation::restoreEntities(RestoreEntitiesOperation snapshot) {
    return DocumentOperation(std::move(snapshot));
}

void remapDocumentOperation(DocumentOperation& operation, std::span<const EntityIdRemap> entityRemaps,
                            std::span<const AssetIdRemap> assetRemaps) {
    const auto remapEntity = [&](scene::EntityId& entity) {
        for (const auto& remap : entityRemaps) {
            if (entity == remap.from) {
                entity = remap.to;
                return;
            }
        }
    };
    const auto remapAsset = [&](asset::AssetId& asset) {
        for (const auto& remap : assetRemaps) {
            if (asset == remap.from) {
                asset = remap.to;
                return;
            }
        }
    };

    std::visit(
            [&](auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, BooleanOperation>) {
                    remapEntity(value.target);
                    remapEntity(value.tool);
                } else if constexpr (std::is_same_v<T, UpdateCurveOperation>) {
                    remapEntity(value.entity);
                } else if constexpr (std::is_same_v<T, UpdateGeometryOperation>) {
                    remapEntity(value.request.entity);
                    remapAsset(value.request.sourceGeometry);
                    remapAsset(value.request.targetGeometry);
                } else if constexpr (std::is_same_v<T, UpdateEntityTransformsOperation> ||
                                     std::is_same_v<T, CopyEntityTransformsOperation>) {
                    for (auto& update : value.updates)
                        remapEntity(update.entity);
                } else if constexpr (std::is_same_v<T, RemoveEntitiesOperation>) {
                    for (auto& entity : value.entities)
                        remapEntity(entity);
                } else if constexpr (std::is_same_v<T, RestoreEntitiesOperation>) {
                    for (auto& entity : value.entities) {
                        remapEntity(entity.originalId);
                        remapEntity(entity.parent);
                        for (auto& child : entity.children)
                            remapEntity(child);
                        remapAsset(entity.geometry);
                        for (auto& material : entity.materialSlots)
                            remapAsset(material);
                    }
                    for (auto& geometry : value.geometryAssets)
                        remapAsset(geometry.originalId);
                }
            },
            operation.data());
}

}  // namespace mulan::editor
