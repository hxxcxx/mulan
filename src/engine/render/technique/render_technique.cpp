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
        .depthWrite = true,
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
        .depthWrite = true,
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
        .depthWrite = false,
        .sampleTextures = false,
    };
}

constexpr TechniqueDesc kSolidLit = makeSolidLit();
constexpr TechniqueDesc kSurfacePBR = makeSurfacePBR();
constexpr TechniqueDesc kEdgeLine = makeEdgeLine();

}  // namespace

const TechniqueDesc& TechniqueRegistry::builtin(RenderTechnique technique) {
    switch (technique) {
    case RenderTechnique::SolidLit: return kSolidLit;
    case RenderTechnique::SurfacePBR: return kSurfacePBR;
    case RenderTechnique::EdgeLine: return kEdgeLine;
    }
    return kSurfacePBR;
}

}  // namespace mulan::engine
