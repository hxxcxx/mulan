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
    SurfaceUnlit,
    SurfaceUnlitTangent,
    SurfaceLegacy,
    SurfaceLegacyTangent,
    SurfacePBR,
    SurfacePBRTangent,
    EdgeLine,
    HighlightSurface,
    HighlightSurfaceTangent,
    HighlightEdge,
    ViewCube,
    ViewCubeLine,
};

enum class MaterialBindingProfile : uint8_t {
    None,
    Unlit,
    Legacy,
    PBR,
};

struct ShaderProgramId {
    const char* vertex = "";
    const char* pixel = "";
};

struct TechniqueDesc {
    RenderTechnique technique = RenderTechnique::SurfacePBR;
    const char* debugName = "Technique";
    ShaderProgramId shader;
    /// 空字符串表示该技术禁止对象批实例化；只替换 VS，fragment 与资源布局保持不变。
    const char* instancedVertexShader = "";
    graphics::VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    bool depthTest = true;
    bool depthWrite = true;
    CompareFunc depthFunc = CompareFunc::LessEqual;
    MaterialBindingProfile materialBindings = MaterialBindingProfile::None;
    /// 面技术默认启用背面剔除；线、透明高亮与 UI 保持 None。
    CullMode cullMode = CullMode::None;
    BlendDesc blend;
};

class TechniqueRegistry {
public:
    static const TechniqueDesc& builtin(RenderTechnique technique);
};

}  // namespace mulan::engine
