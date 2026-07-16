#include "render_technique.h"

namespace mulan::engine {
namespace {

consteval TechniqueDesc makeSolidLit() {
    return TechniqueDesc{
        .technique = RenderTechnique::SolidLit,
        .debugName = "SolidLit",
        .shader = { .vertex = "solid.vert", .pixel = "solid.frag" },
        .instancedVertexShader = "solid_instanced.vert",
        .vertexLayout = graphics::layouts::surface(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = true,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
        .cullMode = CullMode::None,
    };
}

consteval TechniqueDesc makeSurfacePBR() {
    return TechniqueDesc{
        .technique = RenderTechnique::SurfacePBR,
        .debugName = "SurfacePBR",
        .shader = { .vertex = "pbr.vert", .pixel = "pbr.frag" },
        .instancedVertexShader = "pbr_instanced.vert",
        .vertexLayout = graphics::layouts::surface(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = true,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = true,
        .cullMode = CullMode::None,
    };
}

consteval TechniqueDesc makeSurfacePBRTangent() {
    return TechniqueDesc{
        .technique = RenderTechnique::SurfacePBRTangent,
        .debugName = "SurfacePBRTangent",
        .shader = { .vertex = "pbr_tangent.vert", .pixel = "pbr_tangent.frag" },
        .instancedVertexShader = "pbr_tangent_instanced.vert",
        .vertexLayout = graphics::layouts::pbr(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = true,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = true,
        .cullMode = CullMode::None,
    };
}

consteval TechniqueDesc makeEdgeLine() {
    return TechniqueDesc{
        .technique = RenderTechnique::EdgeLine,
        .debugName = "EdgeLine",
        .shader = { .vertex = "edge.vert", .pixel = "edge.frag" },
        .instancedVertexShader = "edge_instanced.vert",
        .vertexLayout = graphics::layouts::position3(),
        .topology = PrimitiveTopology::LineList,
        .depthTest = true,
        .depthWrite = false,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
    };
}

consteval BlendDesc makeAlphaBlend() {
    BlendDesc blend{};
    blend.renderTargets[0].blendEnable = true;
    blend.renderTargets[0].srcBlend = BlendFactor::SrcAlpha;
    blend.renderTargets[0].dstBlend = BlendFactor::InvSrcAlpha;
    blend.renderTargets[0].blendOp = BlendOp::Add;
    blend.renderTargets[0].srcBlendAlpha = BlendFactor::One;
    blend.renderTargets[0].dstBlendAlpha = BlendFactor::InvSrcAlpha;
    blend.renderTargets[0].blendOpAlpha = BlendOp::Add;
    return blend;
}

consteval TechniqueDesc makeHighlightSurface() {
    return TechniqueDesc{
        .technique = RenderTechnique::HighlightSurface,
        .debugName = "HighlightSurface",
        .shader = { .vertex = "solid.vert", .pixel = "highlight_surface.frag" },
        .vertexLayout = graphics::layouts::surface(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = false,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
        .blend = makeAlphaBlend(),
    };
}

consteval TechniqueDesc makeHighlightSurfaceTangent() {
    return TechniqueDesc{
        .technique = RenderTechnique::HighlightSurfaceTangent,
        .debugName = "HighlightSurfaceTangent",
        .shader = { .vertex = "pbr_tangent.vert", .pixel = "highlight_surface.frag" },
        .vertexLayout = graphics::layouts::pbr(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = false,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
        .blend = makeAlphaBlend(),
    };
}

consteval TechniqueDesc makeHighlightEdge() {
    return TechniqueDesc{
        .technique = RenderTechnique::HighlightEdge,
        .debugName = "HighlightEdge",
        .shader = { .vertex = "edge.vert", .pixel = "highlight_edge.frag" },
        .vertexLayout = graphics::layouts::position3(),
        .topology = PrimitiveTopology::LineList,
        .depthTest = true,
        .depthWrite = false,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
    };
}

consteval TechniqueDesc makeViewCube() {
    return TechniqueDesc{
        .technique = RenderTechnique::ViewCube,
        .debugName = "ViewCube",
        .shader = { .vertex = "viewcube.vert", .pixel = "viewcube.frag" },
        .vertexLayout = graphics::layouts::solid(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = true,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
    };
}

consteval TechniqueDesc makeViewCubeLine() {
    return TechniqueDesc{
        .technique = RenderTechnique::ViewCubeLine,
        .debugName = "ViewCubeLine",
        .shader = { .vertex = "edge.vert", .pixel = "viewcube_line.frag" },
        .vertexLayout = graphics::layouts::position3(),
        .topology = PrimitiveTopology::LineList,
        .depthTest = true,
        .depthWrite = false,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
    };
}

constexpr TechniqueDesc kSolidLit = makeSolidLit();
constexpr TechniqueDesc kSurfacePBR = makeSurfacePBR();
constexpr TechniqueDesc kSurfacePBRTangent = makeSurfacePBRTangent();
constexpr TechniqueDesc kEdgeLine = makeEdgeLine();
constexpr TechniqueDesc kHighlightSurface = makeHighlightSurface();
constexpr TechniqueDesc kHighlightSurfaceTangent = makeHighlightSurfaceTangent();
constexpr TechniqueDesc kHighlightEdge = makeHighlightEdge();
constexpr TechniqueDesc kViewCube = makeViewCube();
constexpr TechniqueDesc kViewCubeLine = makeViewCubeLine();

}  // namespace

const TechniqueDesc& TechniqueRegistry::builtin(RenderTechnique technique) {
    switch (technique) {
    case RenderTechnique::SolidLit: return kSolidLit;
    case RenderTechnique::SurfacePBR: return kSurfacePBR;
    case RenderTechnique::SurfacePBRTangent: return kSurfacePBRTangent;
    case RenderTechnique::EdgeLine: return kEdgeLine;
    case RenderTechnique::HighlightSurface: return kHighlightSurface;
    case RenderTechnique::HighlightSurfaceTangent: return kHighlightSurfaceTangent;
    case RenderTechnique::HighlightEdge: return kHighlightEdge;
    case RenderTechnique::ViewCube: return kViewCube;
    case RenderTechnique::ViewCubeLine: return kViewCubeLine;
    }
    return kSurfacePBR;
}

}  // namespace mulan::engine
