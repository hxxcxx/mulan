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

    surface_executor_.execute(ctx);
    surface_tangent_executor_.execute(ctx);
    edge_executor_.execute(ctx);
}

void HighlightStage::setSurfaceDrawCommands(std::span<const MeshDrawCommand> commands) {
    surface_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    surface_tangent_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});

    surface_commands_.clear();
    surface_tangent_commands_.clear();
    for (const auto& command : commands) {
        if (command.pipelineState == surface_tangent_executor_.pipelineState()) {
            surface_tangent_commands_.push_back(command);
        } else {
            surface_commands_.push_back(command);
        }
    }

    surface_executor_.setDrawCommands(surface_commands_);
    surface_tangent_executor_.setDrawCommands(surface_tangent_commands_);
}

void HighlightStage::setEdgeDrawCommands(std::span<const MeshDrawCommand> commands) {
    edge_executor_.setDrawCommands(commands);
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
