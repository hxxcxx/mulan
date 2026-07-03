/**
 * @file view_state.h
 * @brief ViewState 是一帧渲染所需的只读视图快照。
 * @date 2026-07-03
 */
#pragma once

#include <mulan/math/math.h>

#include <cstdint>

namespace mulan::view {

enum class RenderMode {
    Shaded,
    ShadedWithEdges,
    Wireframe
};

struct ViewState {
    engine::Mat4 viewMatrix = engine::Mat4(1.0);
    engine::Mat4 projectionMatrix = engine::Mat4(1.0);
    engine::Vec3 cameraPosition = engine::Vec3(0.0f);

    int width = 0;
    int height = 0;
    float dpiScale = 1.0f;

    RenderMode renderMode = RenderMode::ShadedWithEdges;
    bool showFaces = true;
    bool showEdges = true;
    bool showViewCube = true;
};

} // namespace mulan::view
