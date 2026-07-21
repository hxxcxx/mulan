#include "highlight_stage.h"

namespace mulan::engine {

HighlightStage::HighlightStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                               DrawFallbackResources& fallbackResources, DevicePipelineLibrary& pipelineLibrary)
    : surface_executor_(device, sharedResources, fallbackResources, pipelineLibrary, RenderTechnique::HighlightSurface),
      surface_tangent_executor_(device, sharedResources, fallbackResources, pipelineLibrary,
                                RenderTechnique::HighlightSurfaceTangent),
      edge_executor_(device, sharedResources, fallbackResources, pipelineLibrary, RenderTechnique::HighlightEdge) {
}

ResultVoid HighlightStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!surface_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "HighlightStage surface init failed"));
    }
    if (!surface_tangent_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "HighlightStage tangent surface init failed"));
    }
    if (!edge_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "HighlightStage edge init failed"));
    }
    return {};
}

void HighlightStage::shutdown(RHIDevice&) {
}

void HighlightStage::execute(RenderFrame& frame) {
    DrawExecutionContext ctx;
    ctx.cmd = &frame.cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;

    if (!scene_commands_.surfaces.empty())
        surface_executor_.execute(ctx, scene_commands_.surfaces);
    if (!overlay_commands_.surfaces.empty())
        surface_executor_.execute(ctx, overlay_commands_.surfaces);
    if (!scene_commands_.tangentSurfaces.empty())
        surface_tangent_executor_.execute(ctx, scene_commands_.tangentSurfaces);
    if (!overlay_commands_.tangentSurfaces.empty())
        surface_tangent_executor_.execute(ctx, overlay_commands_.tangentSurfaces);
    if (!scene_commands_.edges.empty())
        edge_executor_.execute(ctx, scene_commands_.edges);
    if (!overlay_commands_.edges.empty())
        edge_executor_.execute(ctx, overlay_commands_.edges);
}

void HighlightStage::setSceneDrawCommands(uint64_t revision, std::span<const MeshDrawCommand> surfaceCommands,
                                          std::span<const MeshDrawCommand> edgeCommands) {
    updateSourceCommands(scene_commands_, revision, surfaceCommands, edgeCommands);
}

void HighlightStage::setOverlayDrawCommands(uint64_t revision, std::span<const MeshDrawCommand> surfaceCommands,
                                            std::span<const MeshDrawCommand> edgeCommands) {
    updateSourceCommands(overlay_commands_, revision, surfaceCommands, edgeCommands);
}

void HighlightStage::updateSourceCommands(SourceCommands& destination, uint64_t revision,
                                          std::span<const MeshDrawCommand> surfaceCommands,
                                          std::span<const MeshDrawCommand> edgeCommands) {
    if (destination.revision == revision)
        return;

    destination.surfaces.clear();
    destination.tangentSurfaces.clear();
    for (const auto& command : surfaceCommands) {
        if (command.pipelineState == surface_tangent_executor_.pipelineState()) {
            destination.tangentSurfaces.push_back(command);
        } else {
            destination.surfaces.push_back(command);
        }
    }
    destination.edges = edgeCommands;
    destination.revision = revision;
}

PipelineState* HighlightStage::surfacePipeline() const {
    return surface_executor_.pipelineState();
}

PipelineState* HighlightStage::surfaceTangentPipeline() const {
    return surface_tangent_executor_.pipelineState();
}

PipelineState* HighlightStage::edgePipeline() const {
    return edge_executor_.pipelineState();
}

}  // namespace mulan::engine
