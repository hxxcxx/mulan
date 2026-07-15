#include "face_stage.h"

namespace mulan::engine {

FaceStage::FaceStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                     DevicePipelineLibrary& pipelineLibrary)
    : solid_executor_(device, sharedResources, pipelineLibrary, RenderTechnique::SolidLit),
      pbr_executor_(device, sharedResources, pipelineLibrary, RenderTechnique::SurfacePBR),
      pbr_tangent_executor_(device, sharedResources, pipelineLibrary, RenderTechnique::SurfacePBRTangent),
      view_cube_executor_(device, sharedResources, pipelineLibrary, RenderTechnique::ViewCube) {
}

ResultVoid FaceStage::init(RHIDevice&, const RenderTargetInfo& target) {
    if (!solid_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SolidLit init failed"));
    }
    if (!pbr_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfacePBR init failed"));
    }
    if (!pbr_tangent_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfacePBRTangent init failed"));
    }
    if (!view_cube_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage ViewCube init failed"));
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
    if (surface_technique_ == SurfaceTechnique::SurfacePBR) {
        pbr_executor_.execute(ctx);
        pbr_tangent_executor_.execute(ctx);
    } else {
        solid_executor_.execute(ctx);
    }
}

void FaceStage::setDrawCommands(std::span<const MeshDrawCommand> commands) {
    solid_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    pbr_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    pbr_tangent_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});

    if (surface_technique_ != SurfaceTechnique::SurfacePBR) {
        solid_executor_.setDrawCommands(commands);
        return;
    }

    pbr_commands_.clear();
    pbr_tangent_commands_.clear();
    for (const auto& command : commands) {
        if (command.pipelineState == pbr_tangent_executor_.pipelineState()) {
            pbr_tangent_commands_.push_back(command);
        } else {
            pbr_commands_.push_back(command);
        }
    }
    pbr_executor_.setDrawCommands(pbr_commands_);
    pbr_tangent_executor_.setDrawCommands(pbr_tangent_commands_);
}

void FaceStage::setSurfaceTechnique(SurfaceTechnique technique) {
    if (surface_technique_ == technique)
        return;
    surface_technique_ = technique;
    solid_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    pbr_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
    pbr_tangent_executor_.setDrawCommands(std::span<const MeshDrawCommand>{});
}

void FaceStage::setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT) {
    pbr_executor_.setIBLTextures(irradiance, prefilter, brdfLUT);
    pbr_tangent_executor_.setIBLTextures(irradiance, prefilter, brdfLUT);
}

PipelineState* FaceStage::pipelineState() const {
    return activeExecutor().pipelineState();
}

PipelineState* FaceStage::tangentPipelineState() const {
    return pbr_tangent_executor_.pipelineState();
}

PipelineState* FaceStage::viewCubePipelineState() const {
    return view_cube_executor_.pipelineState();
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
