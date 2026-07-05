/**
 * @file edge_stage.h
 * @brief EdgeStage 执行边线 bucket 的几何绘制。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_stage.h"
#include "../draw/geometry_draw_executor.h"

#include <span>

namespace mulan::engine {

class EdgeStage final : public RenderStage {
public:
    EdgeStage(RHIDevice& device, RenderResourceCache& gpu, MaterialCache& matCache, const LightEnvironment& lightEnv);

    std::string_view name() const override { return "Edge"; }

    core::Result<void> init(RHIDevice& device, const RenderTargetInfo& target) override;

    void shutdown(RHIDevice& device) override;
    void execute(RenderFrame& frame) override;

    void setDrawCommands(std::span<const MeshDrawCommand> commands);
    PipelineState* pipelineState() const;

private:
    GeometryDrawExecutor draw_executor_;
};

}  // namespace mulan::engine
