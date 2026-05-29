/**
 * @file TransformSystem.cpp
 * @brief 世界变换更新实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "TransformSystem.h"
#include "../World.h"

#include <algorithm>
#include <unordered_set>

namespace mulan::world {

static int depthOf(const World& world, Entity::Id id) {
    int d = 0;
    const Entity* e = world.entityById(id);
    while (e && e->parentId() != Entity::INVALID_ID) {
        ++d;
        e = world.entityById(e->parentId());
    }
    return d;
}

void TransformSystem::update(World& world, float /*dt*/) {
    std::unordered_set<Entity::Id> dirtySet;

    // 1. 收集直接标脏的 Entity
    EntityDirty mask = EntityDirty::Transform | EntityDirty::Parent | EntityDirty::Created;
    world.forEachDirty(mask, [&](Entity* e, uint64_t) {
        dirtySet.insert(e->id());
    });

    // 2. 级联传播：parent 变了 → 所有 children 也受影响
    std::unordered_set<Entity::Id> seen = dirtySet;
    std::vector<Entity::Id> queue(dirtySet.begin(), dirtySet.end());
    while (!queue.empty()) {
        auto id = queue.back();
        queue.pop_back();
        for (auto childId : world.childrenOf(id)) {
            if (seen.insert(childId).second) {
                queue.push_back(childId);
                dirtySet.insert(childId);
                world.markDirty(childId, EntityDirty::Transform);
            }
        }
    }

    if (dirtySet.empty()) return;

    // 3. 按深度排序（保证 parent 在 child 前）
    std::vector<Entity::Id> sorted(dirtySet.begin(), dirtySet.end());
    std::sort(sorted.begin(), sorted.end(), [&](Entity::Id a, Entity::Id b) {
        return depthOf(world, a) < depthOf(world, b);
    });

    // 4. 计算世界变换
    for (auto id : sorted) {
        Entity* e = world.entityById(id);
        if (!e) continue;

        engine::Mat4 parentWorld{1.0};
        if (e->parentId() != Entity::INVALID_ID) {
            const Entity* parent = world.entityById(e->parentId());
            if (parent) parentWorld = parent->worldTransform();
        }
        e->setWorldTransform(parentWorld * e->localTransform());
    }

    // 5. 清除已处理的标记
    world.clearDirty(EntityDirty::Transform | EntityDirty::Parent);
}

} // namespace mulan::world
