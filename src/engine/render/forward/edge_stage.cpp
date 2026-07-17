#include "edge_stage.h"

#include "../device_pipeline_library.h"

namespace mulan::engine {

EdgeStage::EdgeStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                     DrawFallbackResources& fallbackResources, DevicePipelineLibrary& pipelineLibrary)
    : draw_executor_(device, sharedResources, fallbackResources, pipelineLibrary, RenderTechnique::EdgeLine),
      pipeline_library_(pipelineLibrary) {
}

ResultVoid EdgeStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!draw_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "EdgeStage init failed"));
    }
    view_cube_pipeline_ = pipeline_library_.acquire(DevicePipelineKey{
            .technique = RenderTechnique::ViewCubeLine,
            .colorFormat = target.colorFormat,
            .depthFormat = target.depthFormat,
            .sampleCount = target.sampleCount,
            .hasDepth = target.hasDepth,
    });
    if (!view_cube_pipeline_) {
        return std::unexpected(Error::make(ErrorCode::Internal, "EdgeStage ViewCubeLine init failed"));
    }
    return {};
}

void EdgeStage::shutdown(RHIDevice&) {
}

void EdgeStage::execute(RenderFrame& frame) {
    DrawExecutionContext ctx;
    ctx.cmd = &frame.cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;
    draw_executor_.execute(ctx);
}

void EdgeStage::setDrawCommands(std::span<const MeshDrawCommand> commands) {
    draw_executor_.setDrawCommands(commands);
}

PipelineState* EdgeStage::pipelineState() const {
    return draw_executor_.pipelineState();
}

PipelineState* EdgeStage::viewCubePipelineState() const {
    return view_cube_pipeline_;
}

}  // namespace mulan::engine
