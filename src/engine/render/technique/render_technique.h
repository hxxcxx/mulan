#pragma once

#include "../../rhi/render_state.h"

#include <mulan/graphics/vertex/vertex_layout.h>

#include <cstdint>
#include <string_view>

namespace mulan::engine {

enum class RenderTechnique : uint8_t {
    SurfacePBR,
    EdgeLine,
};

struct ShaderProgramId {
    const char* vertex = "";
    const char* pixel = "";
};

struct TechniqueDesc {
    RenderTechnique technique = RenderTechnique::SurfacePBR;
    const char* debugName = "Technique";
    ShaderProgramId shader;
    graphics::VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    bool depthWrite = true;
    bool sampleTextures = false;
};

class TechniqueRegistry {
public:
    static const TechniqueDesc& builtin(RenderTechnique technique);
};

} // namespace mulan::engine
