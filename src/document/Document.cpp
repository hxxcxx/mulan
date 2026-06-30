/**
 * @file Document.cpp
 * @brief Document 实现 — 拥有 World + B-Rep 源
 * @author hxxcxx
 * @date 2026-06-30
 */
#include "Document.h"

#include <mulan/world/World.h>
#include <mulan/world/Entity.h>
#include "SolidGeometryData.h"

#include <TopoDS_Shape.hxx>

namespace mulan::document {

Document::Document(std::string displayName)
    : m_world(std::make_unique<world::World>())
    , m_displayName(std::move(displayName))
{}

Document::~Document() = default;

world::Entity* Document::addSolid(const TopoDS_Shape& shape, std::string name) {
    auto* entity = m_world->createEntity(std::move(name));
    auto geo = std::make_unique<SolidGeometryData>(shape);
    entity->setGeometry(std::move(geo));
    return entity;
}

} // namespace mulan::document
