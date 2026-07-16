/**
 * @file edge_stage.h
 * @brief EdgeStage 执行边线 bucket 的几何绘制。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../draw/geometry_draw_executor.h"
#include "../frame/render_frame.h"
#include "../frame/render_target_info.h"

#include <mulan/core/result/error.h>

#include <span>

namespace mulan::engine {

class GeometryDrawSharedResources;
class DevicePipelineLibrary;

class EdgeStage final {
public:
    EdgeStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources, DevicePipelineLibrary& pipelineLibrary);

    ResultVoid init(RHIDevice& device, const RenderTargetInfo& target);

    void shutdown(RHIDevice& device);
    void execute(RenderFrame& frame);

    void setDrawCommands(std::span<const MeshDrawCommand> commands);
    PipelineState* pipelineState() const;
    PipelineState* viewCubePipelineState() const;

private:
    GeometryDrawExecutor draw_executor_;
    DevicePipelineLibrary& pipeline_library_;
    PipelineState* view_cube_pipeline_ = nullptr;
};

}  // namespace mulan::engine
