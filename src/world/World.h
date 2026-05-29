/**
 * @file World.h
 * @brief World — 实体容器 + 脏标记 + children 索引
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "Entity.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::world {

class World {
public:
    World() = default;
    ~World() = default;

    // 创建/销毁
    Entity* createEntity(std::string name);
    void destroyEntity(Entity::Id id);

    // 查找
    Entity* entityById(Entity::Id id);
    const Entity* entityById(Entity::Id id) const;

    // children 索引
    void addChild(Entity::Id parentId, Entity::Id childId);
    void removeChild(Entity::Id parentId, Entity::Id childId);
    const std::vector<Entity::Id>& childrenOf(Entity::Id parentId) const;

    // 脏标记（System 消费）
    void markDirty(Entity::Id id, EntityDirty d);

    uint64_t getDirtyFlags(Entity::Id id) const;

    template<typename Func>
    void forEachDirty(EntityDirty mask, Func&& fn);

    void clearDirty(EntityDirty mask);

    // 数量
    size_t entityCount() const { return m_entities.size(); }

private:
    struct Slot {
        uint32_t generation = 0;
        bool     alive = false;
    };

    Entity::Id allocateId();
    uint32_t indexOf(Entity::Id id) const { return static_cast<uint32_t>(id & Entity::INDEX_MASK); }

    std::vector<Slot>                         m_slots;
    std::vector<Entity::Id>                   m_freeIndices;
    std::unordered_map<Entity::Id, std::unique_ptr<Entity>> m_entities;
    std::unordered_map<Entity::Id, std::vector<Entity::Id>> m_children;
    std::unordered_map<Entity::Id, uint64_t>  m_dirty;
};

// --- 模板实现 ---

template<typename Func>
void World::forEachDirty(EntityDirty mask, Func&& fn) {
    uint64_t maskValue = dirtyValue(mask);
    for (auto& [id, flags] : m_dirty) {
        if (flags & maskValue) {
            Entity* e = entityById(id);
            if (e) fn(e, flags);
        }
    }
}

} // namespace mulan::world
