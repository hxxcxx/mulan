#include "document.h"

#include "solid_geometry_data.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/scene/scene.h>

#include <TopoDS_Shape.hxx>

namespace mulan::document {

Document::Document(std::string displayName)
    : scene_(std::make_unique<scene::Scene>())
    , assets_(std::make_unique<asset::AssetLibrary>())
    , display_name_(std::move(displayName))
{}

Document::~Document() = default;

scene::EntityId Document::addSolid(const TopoDS_Shape& shape, std::string name) {
    std::string solidName = std::move(name);

    auto geometry = std::make_unique<SolidGeometryData>(shape);
    auto faceMesh = geometry->faceMesh();
    auto edgeMesh = geometry->edgeMesh();
    const auto bounds = geometry->bounds();

    auto* brep = assets_->create<asset::BRepAsset>(solidName);
    if (brep)
        brep->setRenderMeshes(std::move(faceMesh), std::move(edgeMesh));

    const auto sceneId = addSceneInstance(solidName, brep ? brep->id() : asset::AssetId::invalid());
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

DocumentSceneMirrorStats Document::sceneMirrorStats() const {
    DocumentSceneMirrorStats stats;
    stats.sceneEntityCount = scene_ ? scene_->entityCount() : 0;
    stats.assetCount = assets_ ? assets_->count() : 0;
    stats.brepAssetCount = assets_ ? assets_->count(asset::AssetKind::BRep) : 0;
    return stats;
}

} // namespace mulan::document
