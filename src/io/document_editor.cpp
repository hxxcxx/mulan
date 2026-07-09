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

bool DocumentEditor::updateCurveAsset(scene::EntityId entity, asset::AssetId geometry, asset::CurveElementId element,
                                      asset::CurvePrimitive primitive) {
    if (!element.valid()) {
        return false;
    }

    asset::CurveAsset* curve = curveAsset(geometry);
    if (!curve || !curve->update(element, std::move(primitive))) {
        return false;
    }

    return document_.markGeometryChanged(entity, curve->localBounds());
}

bool DocumentEditor::updateFaceAsset(scene::EntityId entity, asset::AssetId geometry, asset::FaceDefinition face) {
    asset::FaceAsset* faceAsset = this->faceAsset(geometry);
    if (!faceAsset) {
        return false;
    }

    faceAsset->setFace(std::move(face));
    return document_.markGeometryChanged(entity, faceAsset->localBounds());
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
    if (!document_.scene() || !geometry) {
        return 0;
    }

    size_t count = 0;
    document_.scene()->forEachEntity([&](scene::EntityId entity) {
        const auto* component = document_.scene()->geometry(entity);
        if (component && component->geometry == geometry) {
            ++count;
        }
    });
    return count;
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

    return asset::AssetId::invalid();
}

bool DocumentEditor::setEntityGeometry(scene::EntityId entity, asset::AssetId geometry) {
    if (!document_.scene() || !document_.assets() || !document_.scene()->isValid(entity)) {
        return false;
    }

    if (!document_.scene()->setGeometry(entity, geometry)) {
        return false;
    }

    math::AABB3 bounds = math::AABB3::empty();
    if (const auto* asset = dynamic_cast<const asset::GeometryAsset*>(document_.assets()->asset(geometry))) {
        bounds = asset->localBounds();
    }
    return document_.markGeometryChanged(entity, bounds);
}

bool DocumentEditor::removeGeometryAsset(asset::AssetId geometry) {
    if (!document_.assets() || !geometry) {
        return false;
    }

    document_.assets()->remove(geometry);
    document_.markDirty();
    return true;
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
