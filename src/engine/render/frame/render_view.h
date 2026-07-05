#pragma once

#include <mulan/math/math.h>

#include <cstdint>

namespace mulan::engine {

struct RenderView {
    math::Mat4 viewMatrix = math::Mat4(1.0);
    math::Mat4 projectionMatrix = math::Mat4(1.0);
    math::Mat4 viewProjectionMatrix = math::Mat4(1.0);
    math::Vec3 cameraPosition = math::Vec3(0.0);

    uint32_t width = 0;
    uint32_t height = 0;

    bool showFaces = true;
    bool showEdges = true;
    bool showViewCube = true;
    bool showOverlay = true;
};

} // namespace mulan::engine
