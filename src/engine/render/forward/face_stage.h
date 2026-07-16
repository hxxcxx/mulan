/**
 * @file face_stage.h
 * @brief FaceStage 执行实体表面 bucket 的几何绘制。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_stage.h"
#include "../draw/geometry_draw_executor.h"
#include "../frontend/render_request.h"

#include <span>
#include <vector>

namespace mulan::engine {

class GeometryDrawSharedResources;
class DevicePipelineLibrary;

class FaceStage final : public RenderStage {
public:
    FaceStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources, DevicePipelineLibrary& pipelineLibrary);

    std::string_view name() const override { return "Face"; }

    ResultVoid init(RHIDevice& device, const RenderTargetInfo& target) override;

    void shutdown(RHIDevice& device) override;
    void execute(RenderFrame& frame) override;

    void setDrawCommands(std::span<const MeshDrawCommand> commands);
    void setSurfaceTechnique(SurfaceTechnique technique);
    void setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT);

    PipelineState* pipelineState() const;
    PipelineState* tangentPipelineState() const;
    PipelineState* viewCubePipelineState() const;
    Texture* defaultWhiteTexture() const;
    Sampler* defaultSampler() const;

private:
    GeometryDrawExecutor& activeExecutor();
    const GeometryDrawExecutor& activeExecutor() const;

    GeometryDrawExecutor solid_executor_;
    GeometryDrawExecutor pbr_executor_;
    GeometryDrawExecutor pbr_tangent_executor_;
    GeometryDrawExecutor view_cube_executor_;
    std::vector<MeshDrawCommand> pbr_commands_;
    std::vector<MeshDrawCommand> pbr_tangent_commands_;
    SurfaceTechnique surface_technique_ = SurfaceTechnique::SolidLit;
};

}  // namespace mulan::engine
