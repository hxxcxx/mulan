#include "document_editor.h"

#include "document.h"

#include <mulan/asset/asset_library.h>
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

    document_.markGeometryChanged(entity, curve->localBounds());
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

bool DocumentEditor::updateCurve(scene::EntityId entity, asset::CurveElementId element,
                                 asset::CurvePrimitive primitive) {
    if (!element.valid()) {
        return false;
    }

    asset::CurveAsset* curve = curveAssetFor(entity);
    if (!curve || !curve->update(element, std::move(primitive))) {
        return false;
    }

    return document_.markGeometryChanged(entity, curve->localBounds());
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

}  // namespace mulan::io
