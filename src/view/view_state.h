/**
 * @file view_state.h
 * @brief ViewState —— 一帧渲染所需的只读视图快照
 * @date 2026-07-03
 *
 * 由 ViewContext/ViewContext 每帧生成，Renderer 只消费不回写。
 * 作为 UI 线程与未来渲染线程之间的边界数据。
 */

#pragma once

#include "mulan/engine/math/math.h"

#include <cstdint>

namespace mulan::view {

struct ViewState {
    engine::Mat4 viewMatrix       = engine::Mat4(1.0);
    engine::Mat4 projectionMatrix = engine::Mat4(1.0);
    engine::Vec3 cameraPosition   = engine::Vec3(0.0f);

    int   width     = 0;
    int   height    = 0;
    float dpiScale  = 1.0f;
};

} // namespace mulan::view
