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

    // 保持原有 family 顺序，并在每个 family 内维持 Scene -> Overlay。
    executeOpaqueSource(unlit_executor_, ctx, scene_commands_.unlit);
    executeOpaqueSource(unlit_executor_, ctx, overlay_commands_.unlit);
    executeOpaqueSource(unlit_tangent_executor_, ctx, scene_commands_.unlitTangent);
    executeOpaqueSource(unlit_tangent_executor_, ctx, overlay_commands_.unlitTangent);
    executeOpaqueSource(legacy_executor_, ctx, scene_commands_.legacy);
    executeOpaqueSource(legacy_executor_, ctx, overlay_commands_.legacy);
    executeOpaqueSource(legacy_tangent_executor_, ctx, scene_commands_.legacyTangent);
    executeOpaqueSource(legacy_tangent_executor_, ctx, overlay_commands_.legacyTangent);
    executeOpaqueSource(pbr_executor_, ctx, scene_commands_.pbr);
    executeOpaqueSource(pbr_executor_, ctx, overlay_commands_.pbr);
    executeOpaqueSource(pbr_tangent_executor_, ctx, scene_commands_.pbrTangent);
    executeOpaqueSource(pbr_tangent_executor_, ctx, overlay_commands_.pbrTangent);
    executeTranslucentSource(ctx, scene_commands_.translucent);
    executeTranslucentSource(ctx, overlay_commands_.translucent);
}

void FaceStage::setDrawCommands(CommandSource source, uint64_t revision, std::span<const MeshDrawCommand> commands) {
    switch (source) {
    case CommandSource::Scene: updateSourceCommands(scene_commands_, revision, commands); return;
    case CommandSource::Overlay: updateSourceCommands(overlay_commands_, revision, commands); return;
    }
}

void FaceStage::updateSourceCommands(SourceCommands& destination, uint64_t revision,
                                     std::span<const MeshDrawCommand> commands) {
    if (destination.revision == revision)
        return;

    destination.unlit.clear();
    destination.unlitTangent.clear();
    destination.legacy.clear();
    destination.legacyTangent.clear();
    destination.pbr.clear();
    destination.pbrTangent.clear();
    destination.translucent.clear();
    for (const auto& command : commands) {
        if (command.translucent) {
            destination.translucent.push_back(command);
            continue;
        }
        switch (command.surfaceFamily) {
        case SurfacePipelineFamily::Unlit: destination.unlit.push_back(command); break;
        case SurfacePipelineFamily::UnlitTangent: destination.unlitTangent.push_back(command); break;
        case SurfacePipelineFamily::Legacy: destination.legacy.push_back(command); break;
        case SurfacePipelineFamily::LegacyTangent: destination.legacyTangent.push_back(command); break;
        case SurfacePipelineFamily::PBR: destination.pbr.push_back(command); break;
        case SurfacePipelineFamily::PBRTangent: destination.pbrTangent.push_back(command); break;
        }
    }
    destination.revision = revision;
}

void FaceStage::executeOpaqueSource(GeometryDrawExecutor& executor, const DrawExecutionContext& context,
                                    std::span<const MeshDrawCommand> commands) {
    if (!commands.empty())
        executor.execute(context, commands);
}

void FaceStage::executeTranslucentSource(const DrawExecutionContext& context,
                                         std::span<const MeshDrawCommand> commands) {
    for (const MeshDrawCommand& command : commands) {
        executorFor(command.surfaceFamily).execute(context, std::span<const MeshDrawCommand>(&command, 1));
    }
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
