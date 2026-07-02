#include "document.h"

#include <mulan/world/World.h>
#include <mulan/world/Entity.h>
#include <mulan/scene/scene.h>
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
    auto* entity = world_->createEntity(std::move(name));
    auto geo = std::make_unique<SolidGeometryData>(shape);
    entity->setGeometry(std::move(geo));
    return entity;
}

} // namespace mulan::document
