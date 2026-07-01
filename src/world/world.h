/**
 * @file world.h
 * @brief World — 实体容器 + 脏标记 + children 索引
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "entity.h"
#include "system/system.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::world {

class World {
public:
    World() = default;
    ~World();

    // 创建/销毁
    Entity* createEntity(std::string name);
    void destroyEntity(Entity::Id id);

    // 查找 + 校验
    Entity* entityById(Entity::Id id);
    const Entity* entityById(Entity::Id id) const;
    bool isValid(Entity::Id id) const;

    // 父子关系（统一入口：循环检测 + children索引 + Entity字段 + 标脏）
    bool setParent(Entity::Id childId, Entity::Id parentId);

    // children 索引（World 维护，setParent 自动更新）
    void addChild(Entity::Id parentId, Entity::Id childId);
    void removeChild(Entity::Id parentId, Entity::Id childId);
    const std::vector<Entity::Id>& childrenOf(Entity::Id parentId) const;

    // System 管理
    void addSystem(std::unique_ptr<System> sys);
    void updateLogic(float dt);

    // 脏标记（System 消费）
    void markDirty(Entity::Id id, EntityDirty d);
    uint64_t getDirtyFlags(Entity::Id id) const;

    template<typename Func>
    void forEachDirty(EntityDirty mask, Func&& fn);

    void clearDirty(EntityDirty mask);

    // 遍历
    template<typename Func>
    void forEachEntity(Func&& fn) {
        for (auto& [id, entity] : entities_)
            fn(entity.get());
    }

    // 数量
    size_t entityCount() const { return entities_.size(); }

private:
    struct Slot {
        uint32_t generation = 1;
        bool     alive = false;
    };

    Entity::Id allocateId();
    uint32_t indexOf(Entity::Id id) const { return static_cast<uint32_t>(id & Entity::INDEX_MASK); }
    bool detectCycle(Entity::Id childId, Entity::Id parentId) const;

    std::vector<Slot>                         slots_;
    std::vector<Entity::Id>                   free_indices_;
    std::unordered_map<Entity::Id, std::unique_ptr<Entity>> entities_;
    std::unordered_map<Entity::Id, std::vector<Entity::Id>> children_;
    std::unordered_map<Entity::Id, uint64_t>  dirty_;
    std::vector<std::unique_ptr<System>>      systems_;
};

// --- 模板实现 ---

template<typename Func>
void World::forEachDirty(EntityDirty mask, Func&& fn) {
    uint64_t maskValue = dirtyValue(mask);
    for (auto& [id, flags] : dirty_) {
        if (flags & maskValue) {
            Entity* e = entityById(id);
            if (e) fn(e, flags);
        }
    }
}

} // namespace mulan::world
