/**
 * @file view_state.h
 * @brief ViewState 是一帧渲染所需的只读视图快照。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include <mulan/engine/render/overlay/view_cube_model.h>
#include <mulan/engine/render/frontend/selection_visual_state.h>
#include <mulan/math/math.h>

#include <cstdint>

namespace mulan::view {

enum class RenderMode { Shaded, ShadedWithEdges, Wireframe };
enum class SurfaceShading { SolidLit, SurfacePBR };

struct ViewState {
    math::Mat4 viewMatrix = math::Mat4(1.0);
    math::Mat4 projectionMatrix = math::Mat4(1.0);
    math::Vec3 cameraPosition = math::Vec3(0.0f);

    int width = 0;
    int height = 0;
    float dpiScale = 1.0f;

    RenderMode renderMode = RenderMode::ShadedWithEdges;
    SurfaceShading surfaceShading = SurfaceShading::SolidLit;
    uint32_t hoveredPickId = 0;
    bool hasHoveredPickId = false;
    engine::SelectionVisualState selectionVisuals;
    bool showFaces = true;
    bool showEdges = true;
    bool showOverlays = true;
    bool showViewCube = true;
    engine::ViewCubeLayout viewCubeLayout;
    engine::ViewCubeInteractionState viewCubeInteraction;
};

}  // namespace mulan::view
