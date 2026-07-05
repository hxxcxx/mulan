#include "edge_stage.h"

namespace mulan::engine {

EdgeStage::EdgeStage(RHIDevice& device, RenderResourceCache& gpu,
                     MaterialCache& matCache, const LightEnvironment& lightEnv)
    : pass_(device, gpu, matCache, lightEnv,
            GeometryPassConfig{
                "edge", PrimitiveTopology::LineList,
                false, "Edge", false}) {
}

std::expected<void, core::Error>
EdgeStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!pass_.init(target.colorFormat, target.depthFormat, target.hasDepth)) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal,
                                                "EdgeStage init failed"));
    }
    return {};
}

void EdgeStage::shutdown(RHIDevice&) {
}

void EdgeStage::execute(RenderFrame& frame) {
    PassContext ctx;
    ctx.cmd = &frame.cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;
    pass_.execute(ctx);
}

void EdgeStage::setDrawCommands(std::span<const MeshDrawCommand> commands) {
    pass_.setDrawCommands(commands);
}

PipelineState* EdgeStage::pipelineState() const {
    return pass_.pipelineState();
}

} // namespace mulan::engine
