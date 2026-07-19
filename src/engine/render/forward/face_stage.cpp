#include "face_stage.h"

#include "../device_pipeline_library.h"
#include "../draw/geometry_draw_shared_resources.h"

namespace mulan::engine {

FaceStage::FaceStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                     DrawFallbackResources& fallbackResources, DevicePipelineLibrary& pipelineLibrary)
    : unlit_executor_(device, sharedResources, fallbackResources, pipelineLibrary, RenderTechnique::SurfaceUnlit),
      unlit_tangent_executor_(device, sharedResources, fallbackResources, pipelineLibrary,
                              RenderTechnique::SurfaceUnlitTangent),
      legacy_executor_(device, sharedResources, fallbackResources, pipelineLibrary, RenderTechnique::SurfaceLegacy),
      legacy_tangent_executor_(device, sharedResources, fallbackResources, pipelineLibrary,
                               RenderTechnique::SurfaceLegacyTangent),
      pbr_executor_(device, sharedResources, fallbackResources, pipelineLibrary, RenderTechnique::SurfacePBR),
      pbr_tangent_executor_(device, sharedResources, fallbackResources, pipelineLibrary,
                            RenderTechnique::SurfacePBRTangent),
      pipeline_library_(pipelineLibrary) {
}

ResultVoid FaceStage::init(RHIDevice&, const RenderTargetInfo& target) {
    target_ = target;
    if (!unlit_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfaceUnlit init failed"));
    }
    if (!unlit_tangent_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfaceUnlitTangent init failed"));
    }
    if (!legacy_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfaceLegacy init failed"));
    }
    if (!legacy_tangent_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfaceLegacyTangent init failed"));
    }
    if (!pbr_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfacePBR init failed"));
    }
    if (!pbr_tangent_executor_.init(target.colorFormat, target.depthFormat, target.hasDepth, target.sampleCount)) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage SurfacePBRTangent init failed"));
    }
    view_cube_pipeline_ = pipeline_library_.acquire(DevicePipelineKey{
            .technique = RenderTechnique::ViewCube,
            .colorFormat = target.colorFormat,
            .depthFormat = target.depthFormat,
            .sampleCount = target.sampleCount,
            .hasDepth = target.hasDepth,
    });
    if (!view_cube_pipeline_) {
        return std::unexpected(Error::make(ErrorCode::Internal, "FaceStage ViewCube init failed"));
    }
    initialized_ = true;
    return {};
}

void FaceStage::shutdown(RHIDevice&) {
    initialized_ = false;
}

void FaceStage::execute(RenderFrame& frame) {
    DrawExecutionContext ctx;
    ctx.cmd = &frame.cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;
    unlit_executor_.execute(ctx);
    unlit_tangent_executor_.execute(ctx);
    legacy_executor_.execute(ctx);
    legacy_tangent_executor_.execute(ctx);
    pbr_executor_.execute(ctx);
    pbr_tangent_executor_.execute(ctx);
    for (const MeshDrawCommand& command : translucent_commands_) {
        executorFor(command.surfaceFamily).execute(ctx, std::span<const MeshDrawCommand>(&command, 1));
    }
}

void FaceStage::setDrawCommands(std::span<const MeshDrawCommand> commands) {
    unlit_commands_.clear();
    unlit_tangent_commands_.clear();
    legacy_commands_.clear();
    legacy_tangent_commands_.clear();
    pbr_commands_.clear();
    pbr_tangent_commands_.clear();
    translucent_commands_.clear();
    for (const auto& command : commands) {
        if (command.translucent) {
            translucent_commands_.push_back(command);
            continue;
        }
        switch (command.surfaceFamily) {
        case SurfacePipelineFamily::Unlit: unlit_commands_.push_back(command); break;
        case SurfacePipelineFamily::UnlitTangent: unlit_tangent_commands_.push_back(command); break;
        case SurfacePipelineFamily::Legacy: legacy_commands_.push_back(command); break;
        case SurfacePipelineFamily::LegacyTangent: legacy_tangent_commands_.push_back(command); break;
        case SurfacePipelineFamily::PBR: pbr_commands_.push_back(command); break;
        case SurfacePipelineFamily::PBRTangent: pbr_tangent_commands_.push_back(command); break;
        }
    }
    unlit_executor_.setDrawCommands(unlit_commands_);
    unlit_tangent_executor_.setDrawCommands(unlit_tangent_commands_);
    legacy_executor_.setDrawCommands(legacy_commands_);
    legacy_tangent_executor_.setDrawCommands(legacy_tangent_commands_);
    pbr_executor_.setDrawCommands(pbr_commands_);
    pbr_tangent_executor_.setDrawCommands(pbr_tangent_commands_);
}

void FaceStage::setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT) {
    pbr_executor_.setIBLTextures(irradiance, prefilter, brdfLUT);
    pbr_tangent_executor_.setIBLTextures(irradiance, prefilter, brdfLUT);
}

PipelineState* FaceStage::acquireSurfacePipeline(const SurfacePipelineRequest& request) {
    if (!initialized_)
        return nullptr;
    RenderTechnique technique = RenderTechnique::SurfacePBR;
    switch (request.family) {
    case SurfacePipelineFamily::Unlit: technique = RenderTechnique::SurfaceUnlit; break;
    case SurfacePipelineFamily::UnlitTangent: technique = RenderTechnique::SurfaceUnlitTangent; break;
    case SurfacePipelineFamily::Legacy: technique = RenderTechnique::SurfaceLegacy; break;
    case SurfacePipelineFamily::LegacyTangent: technique = RenderTechnique::SurfaceLegacyTangent; break;
    case SurfacePipelineFamily::PBR: technique = RenderTechnique::SurfacePBR; break;
    case SurfacePipelineFamily::PBRTangent: technique = RenderTechnique::SurfacePBRTangent; break;
    }
    return pipeline_library_.acquire(DevicePipelineKey{
            .technique = technique,
            .colorFormat = target_.colorFormat,
            .depthFormat = target_.depthFormat,
            .sampleCount = target_.sampleCount,
            .hasDepth = target_.hasDepth,
            .alphaMode = request.alphaMode == graphics::AlphaMode::Blend ? graphics::AlphaMode::Blend
                                                                         : graphics::AlphaMode::Opaque,
            .doubleSided = request.doubleSided,
            .reverseWinding = request.reverseWinding,
    });
}

PipelineState* FaceStage::viewCubePipelineState() const {
    return view_cube_pipeline_;
}

GeometryDrawExecutor& FaceStage::executorFor(SurfacePipelineFamily family) {
    switch (family) {
    case SurfacePipelineFamily::Unlit: return unlit_executor_;
    case SurfacePipelineFamily::UnlitTangent: return unlit_tangent_executor_;
    case SurfacePipelineFamily::Legacy: return legacy_executor_;
    case SurfacePipelineFamily::LegacyTangent: return legacy_tangent_executor_;
    case SurfacePipelineFamily::PBR: return pbr_executor_;
    case SurfacePipelineFamily::PBRTangent: return pbr_tangent_executor_;
    }
    return pbr_executor_;
}

}  // namespace mulan::engine
