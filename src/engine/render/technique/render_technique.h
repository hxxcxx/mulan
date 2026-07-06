/**
 * @file render_technique.h
 * @brief RenderTechnique 描述内置渲染技术与 shader/pipeline 配置的映射。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../../rhi/render_state.h"

#include <mulan/graphics/vertex/vertex_layout.h>

#include <cstdint>
#include <string_view>

namespace mulan::engine {

enum class RenderTechnique : uint8_t {
    SolidLit,
    SurfacePBR,
    EdgeLine,
    ViewCube,
    ViewCubeLine,
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
    bool depthTest = true;
    bool depthWrite = true;
    CompareFunc depthFunc = CompareFunc::LessEqual;
    bool sampleTextures = false;
};

class TechniqueRegistry {
public:
    static const TechniqueDesc& builtin(RenderTechnique technique);
};

}  // namespace mulan::engine
