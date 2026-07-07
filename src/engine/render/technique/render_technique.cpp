#include "render_technique.h"

namespace mulan::engine {
namespace {

consteval TechniqueDesc makeSolidLit() {
    return TechniqueDesc{
        .technique = RenderTechnique::SolidLit,
        .debugName = "SolidLit",
        .shader = { .vertex = "solid.vert", .pixel = "solid.frag" },
        .vertexLayout = graphics::layouts::surface(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = true,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = false,
    };
}

consteval TechniqueDesc makeSurfacePBR() {
    return TechniqueDesc{
        .technique = RenderTechnique::SurfacePBR,
        .debugName = "SurfacePBR",
        .shader = { .vertex = "pbr.vert", .pixel = "pbr.frag" },
        .vertexLayout = graphics::layouts::surface(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = true,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = true,
    };
}

consteval TechniqueDesc makeSurfacePBRTangent() {
    return TechniqueDesc{
        .technique = RenderTechnique::SurfacePBRTangent,
        .debugName = "SurfacePBRTangent",
        .shader = { .vertex = "pbr_tangent.vert", .pixel = "pbr_tangent.frag" },
        .vertexLayout = graphics::layouts::pbr(),
        .topology = PrimitiveTopology::TriangleList,
        .depthTest = true,
        .depthWrite = true,
        .depthFunc = CompareFunc::LessEqual,
        .sampleTextures = true,
    };
}

consteval TechniqueDesc makeEdgeLine() {
    return TechniqueDesc{
        .technique = RenderTechnique::EdgeLine,
        .debugName = "EdgeLine",
        .shader = { .vertex = "edge.vert", .pixel = "edge.frag" },
        .vertexLayout = graphics::layouts::surface(),
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
        .shader = { .vertex = "solid.vert", .pixel = "viewcube.frag" },
        .vertexLayout = graphics::layouts::surface(),
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
        .vertexLayout = graphics::layouts::surface(),
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
constexpr TechniqueDesc kViewCube = makeViewCube();
constexpr TechniqueDesc kViewCubeLine = makeViewCubeLine();

}  // namespace

const TechniqueDesc& TechniqueRegistry::builtin(RenderTechnique technique) {
    switch (technique) {
    case RenderTechnique::SolidLit: return kSolidLit;
    case RenderTechnique::SurfacePBR: return kSurfacePBR;
    case RenderTechnique::SurfacePBRTangent: return kSurfacePBRTangent;
    case RenderTechnique::EdgeLine: return kEdgeLine;
    case RenderTechnique::ViewCube: return kViewCube;
    case RenderTechnique::ViewCubeLine: return kViewCubeLine;
    }
    return kSurfacePBR;
}

}  // namespace mulan::engine
