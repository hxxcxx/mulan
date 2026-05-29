/**
 * @file BoundsSystem.cpp
 * @brief 包围盒更新实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "BoundsSystem.h"
#include "../World.h"

namespace mulan::world {

void BoundsSystem::update(World& world, float /*dt*/) {
    EntityDirty mask = EntityDirty::Geometry | EntityDirty::Transform | EntityDirty::Created;

    world.forEachDirty(mask, [&](Entity* e, uint64_t) {
        if (e->geometry()) {
            engine::AABB local = e->geometry()->bounds();
            e->setCachedBounds(local.transformed(e->worldTransform()));
        }
    });

    world.clearDirty(EntityDirty::BoundsRelated);
}

} // namespace mulan::world
