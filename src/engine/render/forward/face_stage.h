/**
 * @file face_stage.h
 * @brief FaceStage 执行实体表面 bucket 的几何绘制。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../draw/geometry_draw_executor.h"
#include "../frame/render_frame.h"
#include "../frame/render_target_info.h"
#include "../frontend/render_request.h"
#include "../backend/surface_pipeline_provider.h"

#include <mulan/core/result/error.h>

#include <cstdint>
#include <span>
#include <vector>

namespace mulan::engine {

class GeometryDrawSharedResources;
class DrawFallbackResources;
class DevicePipelineLibrary;

class FaceStage final : public SurfacePipelineProvider {
public:
    FaceStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources, DrawFallbackResources& fallbackResources,
              DevicePipelineLibrary& pipelineLibrary);

    ResultVoid init(RHIDevice& device, const RenderTargetInfo& target);

    void shutdown(RHIDevice& device);
    void execute(RenderFrame& frame);

    void setSceneDrawCommands(uint64_t revision, std::span<const MeshDrawCommand> commands);
    void setOverlayDrawCommands(uint64_t revision, std::span<const MeshDrawCommand> commands);
    void setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT);

    PipelineState* acquireSurfacePipeline(const SurfacePipelineRequest& request) override;

    PipelineState* viewCubePipelineState() const;

private:
    struct SourceCommands {
        uint64_t revision = 0;
        std::vector<MeshDrawCommand> unlit;
        std::vector<MeshDrawCommand> unlitTangent;
        std::vector<MeshDrawCommand> legacy;
        std::vector<MeshDrawCommand> legacyTangent;
        std::vector<MeshDrawCommand> pbr;
        std::vector<MeshDrawCommand> pbrTangent;
        std::vector<MeshDrawCommand> translucent;
    };

    static void updateSourceCommands(SourceCommands& destination, uint64_t revision,
                                     std::span<const MeshDrawCommand> commands);
    static void executeOpaqueSource(GeometryDrawExecutor& executor, const DrawExecutionContext& context,
                                    std::span<const MeshDrawCommand> commands);
    void executeTranslucentSource(const DrawExecutionContext& context, std::span<const MeshDrawCommand> commands);
    GeometryDrawExecutor& executorFor(SurfacePipelineFamily family);

    GeometryDrawExecutor unlit_executor_;
    GeometryDrawExecutor unlit_tangent_executor_;
    GeometryDrawExecutor legacy_executor_;
    GeometryDrawExecutor legacy_tangent_executor_;
    GeometryDrawExecutor pbr_executor_;
    GeometryDrawExecutor pbr_tangent_executor_;
    DevicePipelineLibrary& pipeline_library_;
    PipelineState* view_cube_pipeline_ = nullptr;
    RenderTargetInfo target_;
    bool initialized_ = false;
    SourceCommands scene_commands_;
    SourceCommands overlay_commands_;
};

}  // namespace mulan::engine
