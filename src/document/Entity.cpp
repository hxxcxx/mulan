/**
 * @file Entity.cpp
 * @brief Entity 实现
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "Entity.h"

namespace mulan::document {

Entity::Entity(EntityId id, std::string name)
    : m_id(id)
    , m_name(std::move(name))
{}

void Entity::setMetadata(std::string key, Metadata value) {
    m_metadata.emplace(std::move(key), std::move(value));
}

const Entity::Metadata* Entity::getMetadata(const std::string& key) const {
    auto it = m_metadata.find(key);
    return it != m_metadata.end() ? &it->second : nullptr;
}

bool Entity::removeMetadata(const std::string& key) {
    return m_metadata.erase(key) > 0;
}

} // namespace mulan::Document
