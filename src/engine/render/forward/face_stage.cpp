#include "face_stage.h"

namespace mulan::engine {

FaceStage::FaceStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources)
    : solid_executor_(device, sharedResources, RenderTechnique::SolidLit),
      pbr_executor_(device, sharedResources, RenderTechnique::SurfacePBR) {
}

core::Result<void> FaceStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!solid_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth)) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "FaceStage SolidLit init failed"));
    }
    if (!pbr_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth)) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "FaceStage SurfacePBR init failed"));
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
    activeExecutor().execute(ctx);
}

void FaceStage::setDrawCommands(std::span<const MeshDrawCommand> commands) {
    solid_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    pbr_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    activeExecutor().setDrawCommands(commands);
}

void FaceStage::setSurfaceTechnique(SurfaceTechnique technique) {
    if (surface_technique_ == technique)
        return;
    surface_technique_ = technique;
    solid_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    pbr_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
}

void FaceStage::setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT) {
    pbr_executor_.setIBLTextures(irradiance, prefilter, brdfLUT);
}

PipelineState* FaceStage::pipelineState() const {
    return activeExecutor().pipelineState();
}

PipelineState* FaceStage::viewCubePipelineState() const {
    return pbr_executor_.pipelineState();
}

Texture* FaceStage::defaultWhiteTexture() const {
    return activeExecutor().defaultWhiteTexture();
}

Sampler* FaceStage::defaultSampler() const {
    return activeExecutor().defaultSampler();
}

GeometryDrawExecutor& FaceStage::activeExecutor() {
    return surface_technique_ == SurfaceTechnique::SurfacePBR ? pbr_executor_ : solid_executor_;
}

const GeometryDrawExecutor& FaceStage::activeExecutor() const {
    return surface_technique_ == SurfaceTechnique::SurfacePBR ? pbr_executor_ : solid_executor_;
}

}  // namespace mulan::engine
