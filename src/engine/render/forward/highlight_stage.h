/**
 * @file highlight_stage.h
 * @brief HighlightStage 独立执行 hover / selected 的交互高亮绘制。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "render_stage.h"
#include "../draw/geometry_draw_executor.h"

#include <span>
#include <vector>

namespace mulan::engine {

class GeometryDrawSharedResources;

class HighlightStage final : public RenderStage {
public:
    HighlightStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources);

    std::string_view name() const override { return "Highlight"; }

    core::Result<void> init(RHIDevice& device, const RenderTargetInfo& target) override;

    void shutdown(RHIDevice& device) override;
    void execute(RenderFrame& frame) override;

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
