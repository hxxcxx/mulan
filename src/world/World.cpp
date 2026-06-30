/**
 * @file World.cpp
 * @brief World 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "World.h"
#include "system/System.h"

namespace mulan::world {

Entity::Id World::allocateId() {
    uint32_t idx;
    uint32_t gen;
    if (!m_freeIndices.empty()) {
        idx = m_freeIndices.back();
        m_freeIndices.pop_back();
    } else {
        idx = static_cast<uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }
    gen = m_slots[idx].generation;
    m_slots[idx].alive = true;
    return (static_cast<uint64_t>(gen) << Entity::INDEX_BITS) | idx;
}

Entity* World::createEntity(std::string name) {
    Entity::Id id = allocateId();
    auto entity = std::make_unique<Entity>(id, std::move(name));
    Entity* ptr = entity.get();
    ptr->setWorldPtr(this);
    // 在 m_world 就绪后标 Created，确保 dirty 正确传入 World 的 dirty map
    ptr->markDirty(EntityDirty::Created);
    m_entities[id] = std::move(entity);
    return ptr;
}

void World::destroyEntity(Entity::Id id) {
    // 提升 children
    auto it = m_children.find(id);
    if (it != m_children.end()) {
        auto children = std::move(it->second);
        m_children.erase(it);
        for (auto childId : children) {
            Entity* child = entityById(childId);
            if (child) {
                child->m_parent = Entity::INVALID_ID;
                child->markDirty(EntityDirty::Parent | EntityDirty::Transform);
            }
        }
    }

    // 从 parent 的 children 列表中移除
    Entity* e = entityById(id);
    if (e && e->parentId() != Entity::INVALID_ID) {
        removeChild(e->parentId(), id);
    }

    markDirty(id, EntityDirty::Destroyed);

    uint32_t idx = indexOf(id);
    m_slots[idx].alive = false;
    m_slots[idx].generation++;
    m_freeIndices.push_back(static_cast<Entity::Id>(idx));

    m_entities.erase(id);
}

Entity* World::entityById(Entity::Id id) {
    auto it = m_entities.find(id);
    return it != m_entities.end() ? it->second.get() : nullptr;
}

const Entity* World::entityById(Entity::Id id) const {
    auto it = m_entities.find(id);
    return it != m_entities.end() ? it->second.get() : nullptr;
}

void World::addChild(Entity::Id parentId, Entity::Id childId) {
    m_children[parentId].push_back(childId);
}

void World::removeChild(Entity::Id parentId, Entity::Id childId) {
    auto it = m_children.find(parentId);
    if (it != m_children.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), childId), vec.end());
        if (vec.empty()) m_children.erase(it);
    }
}

const std::vector<Entity::Id>& World::childrenOf(Entity::Id parentId) const {
    static const std::vector<Entity::Id> empty;
    auto it = m_children.find(parentId);
    return it != m_children.end() ? it->second : empty;
}

void World::markDirty(Entity::Id id, EntityDirty d) {
    m_dirty[id] |= dirtyValue(d);
}

uint64_t World::getDirtyFlags(Entity::Id id) const {
    auto it = m_dirty.find(id);
    return it != m_dirty.end() ? it->second : 0;
}

void World::clearDirty(EntityDirty mask) {
    uint64_t keep = ~dirtyValue(mask);
    for (auto it = m_dirty.begin(); it != m_dirty.end();) {
        it->second &= keep;
        if (it->second == 0)
            it = m_dirty.erase(it);
        else
            ++it;
    }
}

World::~World() = default;

bool World::isValid(Entity::Id id) const {
    if (id == Entity::INVALID_ID) return false;
    uint32_t idx = static_cast<uint32_t>(id & Entity::INDEX_MASK);
    if (idx >= m_slots.size()) return false;
    return m_slots[idx].alive && (id >> Entity::INDEX_BITS) == m_slots[idx].generation;
}

bool World::setParent(Entity::Id childId, Entity::Id parentId) {
    Entity* child = entityById(childId);
    if (!child) return false;

    // 允许解除 parent
    if (parentId == Entity::INVALID_ID) {
        if (child->parentId() != Entity::INVALID_ID) {
            removeChild(child->parentId(), childId);
        }
        child->setParentId(Entity::INVALID_ID);
        child->markDirty(EntityDirty::Parent | EntityDirty::Transform);
        return true;
    }

    // 目标 parent 必须有效
    Entity* parent = entityById(parentId);
    if (!parent) return false;

    // 循环检测
    if (detectCycle(childId, parentId)) return false;

    // 从旧 parent 移出
    if (child->parentId() != Entity::INVALID_ID) {
        removeChild(child->parentId(), childId);
    }

    // 注册到新 parent
    child->setParentId(parentId);
    addChild(parentId, childId);
    child->markDirty(EntityDirty::Parent | EntityDirty::Transform);
    return true;
}

bool World::detectCycle(Entity::Id childId, Entity::Id parentId) const {
    if (childId == parentId) return true;
    Entity::Id cursor = parentId;
    while (cursor != Entity::INVALID_ID) {
        if (cursor == childId) return true;
        const Entity* e = entityById(cursor);
        if (!e) break;
        cursor = e->parentId();
    }
    return false;
}

void World::addSystem(std::unique_ptr<System> sys) {
    m_systems.push_back(std::move(sys));
    std::sort(m_systems.begin(), m_systems.end(),
              [](const auto& a, const auto& b) { return a->priority() < b->priority(); });
}

void World::updateLogic(float dt) {
    for (auto& sys : m_systems) {
        sys->update(*this, dt);
    }
}

} // namespace mulan::world
