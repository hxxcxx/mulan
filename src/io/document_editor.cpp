#include "document_editor.h"

#include "document.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/asset/tessellated_asset.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/name_component.h>
#include <mulan/scene/components/render_component.h>
#include <mulan/scene/scene.h>

namespace mulan::io {

CurveCreateResult DocumentEditor::createCurve(std::string name, asset::CurvePrimitive primitive) {
    if (!document_.assets() || !document_.scene()) {
        return {};
    }

    std::string curveName = std::move(name);
    auto* curve = document_.assets()->create<asset::CurveAsset>(curveName);
    if (!curve) {
        return {};
    }

    const asset::CurveElementId element = curve->add(std::move(primitive));
    const scene::EntityId entity = document_.addSceneInstance(curveName, curve->id());
    if (!entity) {
        document_.assets()->remove(curve->id());
        return {};
    }

    document_.markGeometryChanged(entity);
    return { entity, element };
}

scene::EntityId DocumentEditor::createFace(std::string name, asset::FaceDefinition face) {
    const scene::EntityId entity = document_.addFace(std::move(name), std::move(face));
    if (entity) {
        document_.markDirty();
    }
    return entity;
}

scene::EntityId DocumentEditor::createMesh(std::string name, std::vector<asset::MeshPrimitive> primitives) {
    const scene::EntityId entity = document_.addMesh(std::move(name), std::move(primitives));
    if (entity) {
        document_.markDirty();
    }
    return entity;
}

scene::EntityId DocumentEditor::createBody(std::string name, modeling::Shape shape) {
    const scene::EntityId entity = document_.addBody(std::move(shape), std::move(name));
    if (entity)
        document_.markDirty();
    return entity;
}

bool DocumentEditor::booleanSubtract(scene::EntityId target, scene::EntityId tool, modeling::BooleanOp op) {
    auto* ops = modeling::ShapeOpsRegistry::instance().ops();
    if (!ops || !document_.assets() || !document_.scene() || target == tool || !document_.scene()->isValid(target) ||
        !document_.scene()->isValid(tool)) {
        return false;
    }

    // 在修改任何文档状态前解析并计算结果，建模失败不会留下部分提交。
    auto resolveBRep = [&](scene::EntityId entity) -> asset::BRepAsset* {
        asset::AssetId geometryId = geometryAssetForEntity(entity);
        if (!geometryId)
            return nullptr;
        auto* asset = document_.assets()->asset(geometryId);
        return dynamic_cast<asset::BRepAsset*>(asset);
    };

    asset::BRepAsset* targetBRep = resolveBRep(target);
    asset::BRepAsset* toolBRep = resolveBRep(tool);
    if (!targetBRep || !toolBRep) {
        return false;
    }

    const modeling::Shape previousTargetShape = targetBRep->shape();
    auto result = [&] {
        MULAN_PROFILE_ZONE_N("ShapeOps::boolean");
        return ops->boolean(previousTargetShape, toolBRep->shape(), op);
    }();
    if (!result) {
        return false;
    }

    const asset::AssetId previousTargetGeometry = targetBRep->id();
    asset::AssetId editedTargetGeometry = previousTargetGeometry;
    bool createdUniqueGeometry = false;

    // target 与副本（包括 tool）共享资产时必须先分离，否则原地 setShape 会篡改所有实例。
    if (geometryReferenceCount(previousTargetGeometry) > 1) {
        editedTargetGeometry = duplicateGeometryAsset(previousTargetGeometry, " Boolean");
        if (!editedTargetGeometry || !setEntityGeometry(target, editedTargetGeometry)) {
            if (editedTargetGeometry) {
                removeGeometryAsset(editedTargetGeometry);
            }
            return false;
        }
        createdUniqueGeometry = true;
        targetBRep = dynamic_cast<asset::BRepAsset*>(document_.assets()->asset(editedTargetGeometry));
        if (!targetBRep) {
            setEntityGeometry(target, previousTargetGeometry);
            removeGeometryAsset(editedTargetGeometry);
            return false;
        }
    }

    targetBRep->setShape(std::move(*result));
    if (!document_.markGeometryChanged(target) || !document_.removeEntity(tool, true)) {
        // 理论上只有外部并发破坏实体时才会走到这里；仍恢复到调用前的几何状态。
        if (createdUniqueGeometry) {
            setEntityGeometry(target, previousTargetGeometry);
            removeGeometryAsset(editedTargetGeometry);
        } else {
            targetBRep->setShape(previousTargetShape);
            document_.markGeometryChanged(target);
        }
        return false;
    }

    document_.markDirty();
    return true;
}

bool DocumentEditor::updateCurve(scene::EntityId entity, asset::CurveElementId element,
                                 asset::CurvePrimitive primitive) {
    if (!element.valid()) {
        return false;
    }

    const asset::AssetId previousGeometry = geometryAssetForEntity(entity);
    asset::AssetId editedGeometry = previousGeometry;
    bool createdUniqueGeometry = false;
    if (geometryReferenceCount(previousGeometry) > 1) {
        editedGeometry = duplicateGeometryAsset(previousGeometry, " Edit");
        if (!editedGeometry || !setEntityGeometry(entity, editedGeometry)) {
            if (editedGeometry) {
                removeGeometryAsset(editedGeometry);
            }
            return false;
        }
        createdUniqueGeometry = true;
    }

    asset::CurveAsset* curve = curveAsset(editedGeometry);
    if (!curve || !curve->update(element, std::move(primitive))) {
        if (createdUniqueGeometry) {
            setEntityGeometry(entity, previousGeometry);
            removeGeometryAsset(editedGeometry);
        }
        return false;
    }

    return document_.markGeometryChanged(entity);
}

bool DocumentEditor::updateCurveAsset(scene::EntityId entity, asset::AssetId geometry, asset::CurveElementId element,
                                      asset::CurvePrimitive primitive) {
    if (!element.valid() || geometryAssetForEntity(entity) != geometry) {
        return false;
    }

    asset::CurveAsset* curve = curveAsset(geometry);
    if (!curve || !curve->update(element, std::move(primitive))) {
        return false;
    }

    return document_.markGeometryChanged(entity);
}

bool DocumentEditor::updateFaceAsset(scene::EntityId entity, asset::AssetId geometry, asset::FaceDefinition face) {
    if (geometryAssetForEntity(entity) != geometry) {
        return false;
    }

    asset::FaceAsset* faceAsset = this->faceAsset(geometry);
    if (!faceAsset) {
        return false;
    }

    faceAsset->setFace(std::move(face));
    return document_.markGeometryChanged(entity);
}

bool DocumentEditor::updateEntityTransform(scene::EntityId entity, const math::Mat4& worldTransform) {
    if (!document_.scene() || !document_.scene()->isValid(entity)) {
        return false;
    }

    if (!document_.scene()->setWorldTransform(entity, worldTransform)) {
        return false;
    }

    document_.markDirty();
    return true;
}

scene::EntityId DocumentEditor::copyEntityWithTransform(scene::EntityId source, const math::Mat4& worldTransform) {
    if (!document_.scene() || !document_.scene()->isValid(source)) {
        return scene::EntityId::invalid();
    }

    const scene::GeometryComponent* geometry = document_.scene()->geometry(source);
    if (!geometry || !geometry->geometry) {
        return scene::EntityId::invalid();
    }

    std::string name = "Entity Copy";
    if (const scene::NameComponent* sourceName = document_.scene()->name(source);
        sourceName && !sourceName->value.empty()) {
        name = sourceName->value + " Copy";
    }

    std::vector<asset::AssetId> materialSlots;
    if (const scene::RenderComponent* render = document_.scene()->render(source)) {
        materialSlots = render->material_slots;
    }

    const scene::EntityId copy =
            document_.addSceneInstance(std::move(name), geometry->geometry, std::move(materialSlots));
    if (!copy) {
        return scene::EntityId::invalid();
    }

    if (const scene::RenderComponent* render = document_.scene()->render(source)) {
        document_.scene()->setVisible(copy, render->visible);
    }
    document_.scene()->setWorldTransform(copy, worldTransform);
    document_.markDirty();
    return copy;
}

bool DocumentEditor::removeEntity(scene::EntityId entity, bool removeGeometryAsset) {
    return document_.removeEntity(entity, removeGeometryAsset);
}

asset::AssetId DocumentEditor::geometryAssetForEntity(scene::EntityId entity) const {
    if (!document_.scene() || !document_.scene()->isValid(entity)) {
        return asset::AssetId::invalid();
    }

    const auto* geometry = document_.scene()->geometry(entity);
    return geometry ? geometry->geometry : asset::AssetId::invalid();
}

size_t DocumentEditor::geometryReferenceCount(asset::AssetId geometry) const {
    return document_.geometryReferenceCount(geometry);
}

asset::AssetId DocumentEditor::duplicateGeometryAsset(asset::AssetId geometry, std::string nameSuffix) {
    if (!document_.assets() || !geometry) {
        return asset::AssetId::invalid();
    }

    const asset::Asset* source = document_.assets()->asset(geometry);
    if (!source) {
        return asset::AssetId::invalid();
    }

    const std::string name = source->name() + std::move(nameSuffix);
    if (const auto* curve = dynamic_cast<const asset::CurveAsset*>(source)) {
        auto* copy = document_.assets()->create<asset::CurveAsset>(name);
        if (!copy) {
            return asset::AssetId::invalid();
        }
        copy->setElements(curve->elements());
        return copy->id();
    }

    if (const auto* face = dynamic_cast<const asset::FaceAsset*>(source)) {
        auto* copy = document_.assets()->create<asset::FaceAsset>(name, face->face());
        return copy ? copy->id() : asset::AssetId::invalid();
    }

    if (const auto* mesh = dynamic_cast<const asset::MeshAsset*>(source)) {
        auto* copy = document_.assets()->create<asset::MeshAsset>(name, mesh->primitives());
        return copy ? copy->id() : asset::AssetId::invalid();
    }

    if (const auto* tessellated = dynamic_cast<const asset::TessellatedAsset*>(source)) {
        auto* copy = document_.assets()->create<asset::TessellatedAsset>(name);
        if (!copy) {
            return asset::AssetId::invalid();
        }
        copy->setRenderMeshes(tessellated->solidMesh(), tessellated->wireMesh());
        return copy->id();
    }

    if (const auto* brep = dynamic_cast<const asset::BRepAsset*>(source)) {
        auto* copy = document_.assets()->create<asset::BRepAsset>(name, brep->shape());
        return copy ? copy->id() : asset::AssetId::invalid();
    }

    return asset::AssetId::invalid();
}

bool DocumentEditor::setEntityGeometry(scene::EntityId entity, asset::AssetId geometry) {
    if (!document_.scene() || !document_.assets() || !document_.scene()->isValid(entity) || !geometry ||
        !document_.assets()->contains(geometry)) {
        return false;
    }

    if (!document_.scene()->setGeometry(entity, geometry)) {
        return false;
    }

    return document_.markGeometryChanged(entity);
}

bool DocumentEditor::removeGeometryAsset(asset::AssetId geometry) {
    return document_.removeGeometryAssetIfUnreferenced(geometry);
}

asset::CurveAsset* DocumentEditor::curveAssetFor(scene::EntityId entity) const {
    if (!document_.scene() || !document_.assets() || !document_.scene()->isValid(entity)) {
        return nullptr;
    }

    const auto* geometry = document_.scene()->geometry(entity);
    if (!geometry || !geometry->geometry) {
        return nullptr;
    }

    return dynamic_cast<asset::CurveAsset*>(document_.assets()->asset(geometry->geometry));
}

asset::CurveAsset* DocumentEditor::curveAsset(asset::AssetId geometry) const {
    return document_.assets() ? dynamic_cast<asset::CurveAsset*>(document_.assets()->asset(geometry)) : nullptr;
}

asset::FaceAsset* DocumentEditor::faceAsset(asset::AssetId geometry) const {
    return document_.assets() ? dynamic_cast<asset::FaceAsset*>(document_.assets()->asset(geometry)) : nullptr;
}

}  // namespace mulan::io
