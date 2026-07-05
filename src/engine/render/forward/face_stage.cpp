#include "face_stage.h"

namespace mulan::engine {

FaceStage::FaceStage(RHIDevice& device, RenderResourceCache& gpu,
                     MaterialCache& matCache, const LightEnvironment& lightEnv)
    : pass_(device, gpu, matCache, lightEnv,
            GeometryPassConfig{
                "pbr", PrimitiveTopology::TriangleList,
                true, "Face", true}) {
}

std::expected<void, core::Error>
FaceStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!pass_.init(target.colorFormat, target.depthFormat, target.hasDepth)) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal,
                                                "FaceStage init failed"));
    }
    return {};
}

void FaceStage::shutdown(RHIDevice&) {
}

void FaceStage::execute(RenderFrame& frame) {
    PassContext ctx;
    ctx.cmd = &frame.cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;
    pass_.execute(ctx);
}

void FaceStage::setDrawCommands(std::span<const MeshDrawCommand> commands) {
    pass_.setDrawCommands(commands);
}

void FaceStage::setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT) {
    pass_.setIBLTextures(irradiance, prefilter, brdfLUT);
}

PipelineState* FaceStage::pipelineState() const {
    return pass_.pipelineState();
}

Texture* FaceStage::defaultWhiteTexture() const {
    return pass_.defaultWhiteTexture();
}

Sampler* FaceStage::defaultSampler() const {
    return pass_.defaultSampler();
}

} // namespace mulan::engine
