#pragma once

#include <mulan/engine/math/aabb.h>

namespace mulan::scene {

struct BoundsComponent {
    engine::AABB world_bounds;
};

} // namespace mulan::scene

