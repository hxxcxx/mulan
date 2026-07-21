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

#include <cstdint>
#include <span>

namespace mulan::engine {

class GeometryDrawSharedResources;
class DrawFallbackResources;
class DevicePipelineLibrary;

class EdgeStage final {
public:
    EdgeStage(RHIDevice& device, GeometryDrawSharedResources& sharedResources, DrawFallbackResources& fallbackResources,
              DevicePipelineLibrary& pipelineLibrary);

    ResultVoid init(RHIDevice& device, const RenderTargetInfo& target);

    void shutdown(RHIDevice& device);
    void execute(RenderFrame& frame);

    void setSceneDrawCommands(uint64_t revision, std::span<const MeshDrawCommand> commands);
    void setOverlayDrawCommands(uint64_t revision, std::span<const MeshDrawCommand> commands);
    PipelineState* pipelineState() const;
    PipelineState* viewCubePipelineState() const;

private:
    struct SourceCommands {
        uint64_t revision = 0;
        std::span<const MeshDrawCommand> commands;
    };

    static void updateSourceCommands(SourceCommands& destination, uint64_t revision,
                                     std::span<const MeshDrawCommand> commands);
    GeometryDrawExecutor draw_executor_;
    DevicePipelineLibrary& pipeline_library_;
    PipelineState* view_cube_pipeline_ = nullptr;
    SourceCommands scene_commands_;
    SourceCommands overlay_commands_;
};

}  // namespace mulan::engine
