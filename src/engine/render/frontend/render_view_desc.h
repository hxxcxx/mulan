#pragma once

#include <mulan/math/math.h>

#include <cstdint>

namespace mulan::engine {

struct RenderViewDesc {
    math::Mat4 viewMatrix{1.0f};
    math::Mat4 projectionMatrix{1.0f};
    math::Vec3 cameraPosition{0.0f};
    uint32_t width = 0;
    uint32_t height = 0;
};

} // namespace mulan::engine
