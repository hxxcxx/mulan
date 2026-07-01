#include "bounds_system.h"
#include "../world.h"

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
