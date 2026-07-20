/**
 * @file render_view.h
 * @brief RenderView 保存 backend 执行一帧渲染所需的视图快照。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../overlay/view_cube_contract.h"

#include <mulan/math/math.h>

#include <cstdint>

namespace mulan::engine {

struct RenderView {
    math::Mat4 viewMatrix = math::Mat4(1.0);
    math::Mat4 projectionMatrix = math::Mat4(1.0);
    math::Point3 cameraPosition = math::Point3::origin();

    uint32_t width = 0;
    uint32_t height = 0;

    bool showViewCube = true;
    bool showOverlay = true;
    ViewCubeLayout viewCubeLayout;
    ViewCubeInteractionState viewCubeInteraction;
};

}  // namespace mulan::engine
