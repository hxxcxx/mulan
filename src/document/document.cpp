#include "document.h"

#include <mulan/world/World.h>
#include <mulan/world/Entity.h>
#include <mulan/scene/scene.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/asset/asset_library.h>
#include "solid_geometry_data.h"

#include <TopoDS_Shape.hxx>

namespace mulan::document {

Document::Document(std::string displayName)
    : world_(std::make_unique<world::World>())
    , scene_(std::make_unique<scene::Scene>())
    , assets_(std::make_unique<asset::AssetLibrary>())
    , display_name_(std::move(displayName))
{}

Document::~Document() = default;

world::Entity* Document::addSolid(const TopoDS_Shape& shape, std::string name) {
    std::string solidName = std::move(name);

    auto* entity = world_->createEntity(solidName);
    auto geo = std::make_unique<SolidGeometryData>(shape);
    entity->setGeometry(std::move(geo));

    // 迁移镜像：旧 world 继续负责当前渲染，同时在 AssetLibrary + Scene 中建立未来数据形态。
    auto* brep = assets_->create<asset::BRepAsset>(solidName);
    addSceneInstance(solidName, brep ? brep->id() : asset::AssetId::invalid());

    return entity;
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
    stats.worldEntityCount = world_ ? world_->entityCount() : 0;
    stats.sceneEntityCount = scene_ ? scene_->entityCount() : 0;
    stats.assetCount = assets_ ? assets_->count() : 0;
    stats.brepAssetCount = assets_ ? assets_->count(asset::AssetKind::BRep) : 0;
    return stats;
}

} // namespace mulan::document
