/**
 * @file render_request.h
 * @brief RenderRequest 描述一次渲染调用的世界快照、视图和显示选项。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "display_mode.h"
#include "render_view_desc.h"
#include "render_world_snapshot.h"
#include "selection_visual_state.h"
#include "../overlay/view_cube_contract.h"

namespace mulan::engine {

struct RenderOptions {
    DisplayMode displayMode = DisplayMode::ShadedWithEdges;
    SelectionVisualState selectionVisuals;
    bool showOverlays = true;
    bool showViewCube = true;
    ViewCubeLayout viewCubeLayout;
    ViewCubeInteractionState viewCubeInteraction;
};

inline bool renderSurfacesEnabled(const RenderOptions& options) {
    switch (options.displayMode) {
    case DisplayMode::Shaded:
    case DisplayMode::ShadedWithEdges: return true;
    case DisplayMode::Wireframe: return false;
    }
    return true;
}

inline bool renderEdgesEnabled(const RenderOptions& options) {
    switch (options.displayMode) {
    case DisplayMode::Shaded: return false;
    case DisplayMode::ShadedWithEdges:
    case DisplayMode::Wireframe: return true;
    }
    return true;
}

struct RenderRequest {
    const RenderWorldSnapshot* sceneWorld = nullptr;
    const RenderWorldSnapshot* overlayWorld = nullptr;
    RenderViewDesc view;
    RenderOptions options;
};

}  // namespace mulan::engine
