/**
 * @file Document.cpp
 * @brief Document 实现
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "Document.h"
#include "Entity.h"

#include <algorithm>

namespace MulanGeo::document {

std::unique_ptr<Document> Document::create() {
    return std::unique_ptr<Document>(new Document());
}

EntityId Document::createEntity(std::string name, std::unique_ptr<Geometry> geometry) {
    auto id = EntityId::generate();
    auto entity = std::make_unique<Entity>(id, std::move(name));
    entity->setGeometry(std::move(geometry));
    m_entities.emplace(id, std::move(entity));
    m_modified = true;
    return id;
}

EntityId Document::createEntity(std::string name) {
    auto id = EntityId::generate();
    auto entity = std::make_unique<Entity>(id, std::move(name));
    m_entities.emplace(id, std::move(entity));
    m_modified = true;
    return id;
}

Entity* Document::findEntity(EntityId id) {
    auto it = m_entities.find(id);
    return it != m_entities.end() ? it->second.get() : nullptr;
}

const Entity* Document::findEntity(EntityId id) const {
    auto it = m_entities.find(id);
    return it != m_entities.end() ? it->second.get() : nullptr;
}

bool Document::removeEntity(EntityId id) {
    auto it = m_entities.find(id);
    if (it == m_entities.end()) return false;

    // 清除子实体的 parentId 引用（使其变为根实体）
    for (auto& [eid, entity] : m_entities) {
        if (entity->parentId() == id) {
            entity->setParentId(EntityId{});
        }
    }

    m_entities.erase(it);
    m_modified = true;
    return true;
}

size_t Document::entityCount() const {
    return m_entities.size();
}

std::vector<EntityId> Document::rootEntityIds() const {
    std::vector<EntityId> roots;
    for (const auto& [id, entity] : m_entities) {
        if (!entity->hasParent()) {
            roots.push_back(id);
        }
    }
    return roots;
}

std::vector<EntityId> Document::childEntityIds(EntityId parentId) const {
    std::vector<EntityId> children;
    for (const auto& [id, entity] : m_entities) {
        if (entity->parentId() == parentId) {
            children.push_back(id);
        }
    }
    return children;
}

void Document::forEachEntity(const EntityCallback& cb) {
    for (auto& [id, entity] : m_entities) {
        cb(*entity);
    }
}

void Document::forEachEntity(const ConstEntityCallback& cb) const {
    for (const auto& [id, entity] : m_entities) {
        cb(*entity);
    }
}

std::string Document::summary() const {
    size_t geoCount = 0;
    size_t meshCount = 0;
    size_t occtCount = 0;

    for (const auto& [id, entity] : m_entities) {
        if (entity->hasGeometry()) {
            ++geoCount;
            switch (entity->geometry()->geometryType()) {
            case GeometryType::Mesh:      ++meshCount; break;
            case GeometryType::OCCTShape: ++occtCount; break;
            }
        }
    }

    return std::to_string(m_entities.size()) + " entities | "
         + std::to_string(geoCount) + " with geometry | "
         + std::to_string(meshCount) + " mesh | "
         + std::to_string(occtCount) + " occt";
}

void Document::clear() {
    m_entities.clear();
    m_modified = true;
}

} // namespace MulanGeo::Document
