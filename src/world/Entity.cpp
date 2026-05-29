/**
 * @file Entity.cpp
 * @brief Entity 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "Entity.h"
#include "World.h"

namespace mulan::world {

Entity::Entity(Id id, std::string name)
    : m_id(id)
    , m_name(std::move(name))
    , m_localTransform(1.0)
    , m_worldTransform(1.0) {
    markDirty(EntityDirty::Created);
}

// --- setter 自动标脏 ---

void Entity::setName(std::string name) {
    if (m_name != name) {
        m_name = std::move(name);
        markDirty(EntityDirty::Name);
    }
}

void Entity::setLocalTransform(const engine::Mat4& t) {
    m_localTransform = t;
    markDirty(EntityDirty::Transform);
}

void Entity::setVisible(bool v) {
    if (m_visible != v) {
        m_visible = v;
        markDirty(EntityDirty::Visibility);
    }
}

void Entity::setSelected(bool s) {
    if (m_selected != s) {
        m_selected = s;
        markDirty(EntityDirty::Selection);
    }
}

void Entity::setMaterialId(uint16_t id) {
    if (m_materialId != id) {
        m_materialId = id;
        markDirty(EntityDirty::Material);
    }
}

void Entity::setGeometry(std::unique_ptr<GeometryData> geo) {
    m_geometry = std::move(geo);
    m_cachedFaceMesh = engine::Mesh{};
    m_cachedEdgeMesh = engine::Mesh{};
    markDirty(EntityDirty::Geometry);
}

const engine::Mesh& Entity::cachedFaceMesh() const {
    if (m_cachedFaceMesh.empty() && m_geometry) {
        m_cachedFaceMesh = m_geometry->faceMesh();
        m_cachedEdgeMesh = m_geometry->edgeMesh();
    }
    return m_cachedFaceMesh;
}

const engine::Mesh& Entity::cachedEdgeMesh() const {
    if (m_cachedEdgeMesh.empty() && m_geometry) {
        m_cachedFaceMesh = m_geometry->faceMesh();
        m_cachedEdgeMesh = m_geometry->edgeMesh();
    }
    return m_cachedEdgeMesh;
}

bool Entity::valid(const World& world) const {
    return m_id != INVALID_ID && world.isValid(m_id);
}

} // namespace mulan::world
