/**
 * @file highlight_stage.h
 * @brief HighlightStage 独立执行 hover / selected 的交互高亮绘制。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "../draw/geometry_draw_executor.h"
#include "../frame/render_frame.h"
#include "../frame/render_target_info.h"

#include <mulan/core/result/error.h>

#include <span>
#include <vector>

namespace mulan::engine {

class GeometryDrawSharedResources;
class DrawFallbackResources;
class DevicePipelineLibrary;

class HighlightStage final {
public:
    HighlightStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                   DrawFallbackResources& fallbackResources, DevicePipelineLibrary& pipelineLibrary);

    ResultVoid init(RHIDevice& device, const RenderTargetInfo& target);

    void shutdown(RHIDevice& device);
    void execute(RenderFrame& frame);

    void setSurfaceDrawCommands(std::span<const MeshDrawCommand> commands);
    void setEdgeDrawCommands(std::span<const MeshDrawCommand> commands);

    PipelineState* surfacePipeline() const;
    PipelineState* surfaceTangentPipeline() const;
    PipelineState* edgePipeline() const;

private:
    GeometryDrawExecutor surface_executor_;
    GeometryDrawExecutor surface_tangent_executor_;
    GeometryDrawExecutor edge_executor_;
    std::vector<MeshDrawCommand> surface_commands_;
    std::vector<MeshDrawCommand> surface_tangent_commands_;
};

}  // namespace mulan::engine
