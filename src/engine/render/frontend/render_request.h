/**
 * @file render_request.h
 * @brief RenderRequest 描述一次渲染调用的世界快照、视图、输出目标和显示选项。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_capture.h"
#include "render_resource_prepare.h"
#include "render_view_desc.h"
#include "render_world_snapshot.h"

#include <cstdint>

namespace mulan::engine {

enum class RenderTargetMode : uint8_t {
    Present,
    Offscreen,
    Capture,
};

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
    bool showSurfaces = true;
    bool showEdges = true;
    bool showOverlays = true;
    bool showViewCube = true;
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

struct RenderOutputDesc {
    RenderTargetMode mode = RenderTargetMode::Present;
    uint32_t width = 0;
    uint32_t height = 0;
    bool readback = false;
    RenderCaptureDesc capture;
};

struct RenderRequest {
    const RenderWorldSnapshot* world = nullptr;
    const RenderResourcePrepareList* prepare = nullptr;
    RenderViewDesc view;
    RenderOutputDesc output;
    RenderOptions options;
};

}  // namespace mulan::engine
