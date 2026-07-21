/**
 * @file view_state.h
 * @brief ViewState 是一帧渲染所需的只读视图快照。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include <mulan/math/math.h>
#include <mulan/render/frontend/display_mode.h>
#include <mulan/render/frontend/selection_visual_state.h>
#include <mulan/render/overlay/view_cube_contract.h>

#include <cstdint>

namespace mulan::view {

struct ViewState {
    math::Mat4 viewMatrix = math::Mat4(1.0);
    math::Mat4 projectionMatrix = math::Mat4(1.0);
    math::Point3 cameraPosition = math::Point3::origin();

    int width = 0;
    int height = 0;

    engine::DisplayMode displayMode = engine::DisplayMode::ShadedWithEdges;
    engine::SelectionVisualState selectionVisuals;
    bool showOverlays = true;
    bool showViewCube = true;
    engine::ViewCubeLayout viewCubeLayout;
    engine::ViewCubeInteractionState viewCubeInteraction;
};

}  // namespace mulan::view
