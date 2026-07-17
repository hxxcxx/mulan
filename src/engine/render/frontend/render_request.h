/**
 * @file render_request.h
 * @brief RenderRequest 描述一次渲染调用的世界快照、视图和显示选项。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "selection_visual_state.h"
#include "render_view_desc.h"
#include "render_world_snapshot.h"
#include "../overlay/view_cube_model.h"

#include <cstdint>

namespace mulan::engine {

enum class DisplayMode : uint8_t {
    Shaded,
    ShadedWithEdges,
    Wireframe,
    HiddenLine,
    XRay,
};

enum class SurfaceTechnique : uint8_t {
    SolidLit,
    SurfacePBR,
};

struct RenderOptions {
    DisplayMode displayMode = DisplayMode::ShadedWithEdges;
    SurfaceTechnique surfaceTechnique = SurfaceTechnique::SolidLit;
    PickId hoveredPickId;
    SelectionVisualState selectionVisuals;
    bool showSurfaces = true;
    bool showEdges = true;
    bool showOverlays = true;
    bool showViewCube = true;
    ViewCubeLayout viewCubeLayout;
    ViewCubeInteractionState viewCubeInteraction;
};

inline bool renderSurfacesEnabled(const RenderOptions& options) {
    switch (options.displayMode) {
    case DisplayMode::Shaded:
    case DisplayMode::ShadedWithEdges:
    case DisplayMode::HiddenLine:
    case DisplayMode::XRay: return options.showSurfaces;
    case DisplayMode::Wireframe: return false;
    }
    return options.showSurfaces;
}

inline bool renderEdgesEnabled(const RenderOptions& options) {
    switch (options.displayMode) {
    case DisplayMode::Shaded: return false;
    case DisplayMode::ShadedWithEdges:
    case DisplayMode::Wireframe:
    case DisplayMode::HiddenLine:
    case DisplayMode::XRay: return options.showEdges;
    }
    return options.showEdges;
}

struct RenderRequest {
    const RenderWorldSnapshot* sceneWorld = nullptr;
    const RenderWorldSnapshot* overlayWorld = nullptr;
    RenderViewDesc view;
    RenderOptions options;
};

}  // namespace mulan::engine
