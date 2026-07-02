#pragma once

#include <mulan/engine/math/math.h>

namespace mulan::scene {

struct TransformComponent {
    engine::Mat4 local{1.0};
    engine::Mat4 world{1.0};
};

} // namespace mulan::scene

