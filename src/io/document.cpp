#include "document.h"

#include "shape_render_geometry.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/sketch_asset.h>
#include <mulan/asset/tessellated_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/scene.h>

#include <TopoDS_Shape.hxx>

namespace mulan::io {

Document::Document(std::string displayName)
    : scene_(std::make_unique<scene::Scene>()),
      assets_(std::make_unique<asset::AssetLibrary>()),
      display_name_(std::move(displayName)) {
}

Document::~Document() = default;

scene::EntityId Document::addShape(const TopoDS_Shape& shape, std::string name) {
    std::string shapeName = std::move(name);

    auto geometry = buildShapeRenderGeometry(shape);

    auto* tess = assets_->create<asset::TessellatedAsset>(shapeName);
    if (tess)
        tess->setRenderMeshes(std::move(geometry.solidMesh), std::move(geometry.wireMesh));

    const auto sceneId = addSceneInstance(shapeName, tess ? tess->id() : asset::AssetId::invalid());
    scene_->setWorldBounds(sceneId, geometry.bounds);
    return sceneId;
}

scene::EntityId Document::addMesh(std::string name, std::vector<asset::MeshPrimitive> primitives) {
    std::string meshName = std::move(name);

    auto* mesh = assets_->create<asset::MeshAsset>(meshName);
    if (!mesh)
        return scene::EntityId::invalid();

    math::AABB3 bounds = math::AABB3::empty();
    std::vector<asset::AssetId> materialSlots;
    materialSlots.reserve(primitives.size());

    for (auto& primitive : primitives) {
        primitive.mesh.computeBounds();
        if (!primitive.mesh.bounds.isEmpty()) {
            bounds.expand(primitive.mesh.bounds);
        }
        materialSlots.push_back(primitive.material);
        mesh->addPrimitive(std::move(primitive.mesh), primitive.material, std::move(primitive.name));
    }

    const auto sceneId = addSceneInstance(meshName, mesh->id(), std::move(materialSlots));
    scene_->setWorldBounds(sceneId, bounds);
    return sceneId;
}

scene::EntityId Document::addSketchLine(std::string name, const math::Point3& start, const math::Point3& end,
                                        asset::SketchElementId* outLineId) {
    std::string sketchName = std::move(name);

    auto* sketch = assets_->create<asset::SketchAsset>(sketchName);
    if (!sketch)
        return scene::EntityId::invalid();

    const auto lineId = sketch->addLine(start, end);
    if (outLineId) {
        *outLineId = lineId;
    }

    const auto sceneId = addSceneInstance(sketchName, sketch->id());
    scene_->setWorldBounds(sceneId, sketch->localBounds());
    markDirty();
    return sceneId;
}

bool Document::updateSketchLine(scene::EntityId entity, asset::SketchElementId lineId, const math::Point3& start,
                                const math::Point3& end) {
    if (!scene_ || !assets_ || !scene_->isValid(entity) || !lineId.valid()) {
        return false;
    }

    const auto* geometry = scene_->geometry(entity);
    if (!geometry || !geometry->geometry) {
        return false;
    }

    auto* sketch = dynamic_cast<asset::SketchAsset*>(assets_->asset(geometry->geometry));
    if (!sketch || !sketch->updateLine(lineId, start, end)) {
        return false;
    }

    scene_->setWorldBounds(entity, sketch->localBounds());
    scene_->markDirty(entity, scene::EntityDirty::RenderRelated | scene::EntityDirty::Bounds);
    markDirty();
    return true;
}

bool Document::removeSketchEntity(scene::EntityId entity) {
    if (!scene_ || !assets_ || !scene_->isValid(entity)) {
        return false;
    }

    asset::AssetId geometryId = asset::AssetId::invalid();
    if (const auto* geometry = scene_->geometry(entity)) {
        geometryId = geometry->geometry;
    }
    if (!geometryId) {
        return false;
    }

    const auto* asset = assets_->asset(geometryId);
    if (!asset || asset->kind() != asset::AssetKind::Sketch) {
        return false;
    }

    scene_->destroyEntity(entity);
    assets_->remove(geometryId);
    markDirty();
    return true;
}

scene::EntityId Document::addSceneInstance(std::string name, asset::AssetId geometry,
                                           std::vector<asset::AssetId> materialSlots) {
    if (!scene_)
        return scene::EntityId::invalid();

    scene::EntityId id = scene_->createEntity(std::move(name));
    scene_->setGeometry(id, geometry);
    scene_->setMaterialSlots(id, std::move(materialSlots));
    return id;
}

}  // namespace mulan::io
