#include "edge_stage.h"

namespace mulan::engine {

EdgeStage::EdgeStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources)
    : draw_executor_(device, sharedResources, RenderTechnique::EdgeLine) {
}

core::Result<void> EdgeStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!draw_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth)) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "EdgeStage init failed"));
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

}  // namespace mulan::engine
