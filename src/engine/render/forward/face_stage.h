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

#include <mulan/core/result/error.h>

#include <span>
#include <vector>

namespace mulan::engine {

class GeometryDrawSharedResources;
class DrawFallbackResources;
class DevicePipelineLibrary;

class FaceStage final {
public:
    FaceStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources, DrawFallbackResources& fallbackResources,
              DevicePipelineLibrary& pipelineLibrary);

    ResultVoid init(RHIDevice& device, const RenderTargetInfo& target);

    void shutdown(RHIDevice& device);
    void execute(RenderFrame& frame);

    void setDrawCommands(std::span<const MeshDrawCommand> commands);
    void setSurfaceTechnique(SurfaceTechnique technique);
    void setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT);

    PipelineState* pipelineState() const;
    PipelineState* tangentPipelineState() const;
    PipelineState* viewCubePipelineState() const;

private:
    GeometryDrawExecutor& activeExecutor();
    const GeometryDrawExecutor& activeExecutor() const;

    GeometryDrawExecutor solid_executor_;
    GeometryDrawExecutor pbr_executor_;
    GeometryDrawExecutor pbr_tangent_executor_;
    DevicePipelineLibrary& pipeline_library_;
    PipelineState* view_cube_pipeline_ = nullptr;
    std::vector<MeshDrawCommand> pbr_commands_;
    std::vector<MeshDrawCommand> pbr_tangent_commands_;
    SurfaceTechnique surface_technique_ = SurfaceTechnique::SolidLit;
};

}  // namespace mulan::engine
