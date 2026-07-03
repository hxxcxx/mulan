#include "document.h"

#include "shape_render_geometry.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/tessellated_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/scene/scene.h>

#include <TopoDS_Shape.hxx>

namespace mulan::document {

Document::Document(std::string displayName)
    : scene_(std::make_unique<scene::Scene>())
    , assets_(std::make_unique<asset::AssetLibrary>())
    , display_name_(std::move(displayName))
{}

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
    if (!mesh) return scene::EntityId::invalid();

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

scene::EntityId Document::addSceneInstance(std::string name,
                                           asset::AssetId geometry,
                                           std::vector<asset::AssetId> materialSlots) {
    if (!scene_) return scene::EntityId::invalid();

    scene::EntityId id = scene_->createEntity(std::move(name));
    scene_->setGeometry(id, geometry);
    scene_->setMaterialSlots(id, std::move(materialSlots));
    return id;
}

} // namespace mulan::document
