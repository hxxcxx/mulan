#pragma once

#include "render_view_desc.h"
#include "render_world_snapshot.h"

#include <cstdint>

namespace mulan::engine {

enum class RenderTargetMode : uint8_t {
    Present,
    Offscreen,
    Capture,
};

struct RenderOptions {
    bool showSurfaces = true;
    bool showEdges = true;
    bool showOverlays = true;
    bool showViewCube = true;
};

struct RenderOutputDesc {
    RenderTargetMode mode = RenderTargetMode::Present;
    uint32_t width = 0;
    uint32_t height = 0;
    bool readback = false;
};

struct RenderRequest {
    const RenderWorldSnapshot* world = nullptr;
    RenderViewDesc view;
    RenderOutputDesc output;
    RenderOptions options;
};

} // namespace mulan::engine
