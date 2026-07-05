#include "face_stage.h"

namespace mulan::engine {

FaceStage::FaceStage(RHIDevice& device, MaterialCache& matCache, const LightEnvironment& lightEnv)
    : draw_executor_(device, matCache, lightEnv, RenderTechnique::SurfacePBR) {
}

core::Result<void> FaceStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!draw_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth)) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "FaceStage init failed"));
    }
    return {};
}

void FaceStage::shutdown(RHIDevice&) {
}

void FaceStage::execute(RenderFrame& frame) {
    DrawExecutionContext ctx;
    ctx.cmd = &frame.cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;
    draw_executor_.execute(ctx);
}

void FaceStage::setDrawCommands(std::span<const MeshDrawCommand> commands) {
    draw_executor_.setDrawCommands(commands);
}

void FaceStage::setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT) {
    draw_executor_.setIBLTextures(irradiance, prefilter, brdfLUT);
}

PipelineState* FaceStage::pipelineState() const {
    return draw_executor_.pipelineState();
}

Texture* FaceStage::defaultWhiteTexture() const {
    return draw_executor_.defaultWhiteTexture();
}

Sampler* FaceStage::defaultSampler() const {
    return draw_executor_.defaultSampler();
}

}  // namespace mulan::engine
