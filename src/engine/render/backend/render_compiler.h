/**
 * @file render_compiler.h
 * @brief RenderCompiler 将 frontend workload 编译为可提交的 MeshDrawCommand 列表。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_compile_context.h"
#include "../frontend/render_workload.h"
#include "../mesh_draw_command.h"

#include <cstddef>
#include <span>
#include <vector>

namespace mulan::engine {

struct RenderCompilerStats {
    size_t surfaceWorkItemCount = 0;
    size_t edgeWorkItemCount = 0;
    size_t highlightSurfaceWorkItemCount = 0;
    size_t highlightEdgeWorkItemCount = 0;
    size_t acceptedSurfaceCommandCount = 0;
    size_t acceptedEdgeCommandCount = 0;
    size_t acceptedHighlightSurfaceCommandCount = 0;
    size_t acceptedHighlightEdgeCommandCount = 0;
    size_t missingGeometryRecordCount = 0;
    size_t emptyGeometryCount = 0;
    size_t missingGpuGeometryCount = 0;
    size_t rejectedContractCount = 0;
    size_t missingPipelineCount = 0;
    size_t objectUboLimitCount = 0;

    void reset() { *this = {}; }
};

class RenderCompiler {
public:
    void compile(const RenderWorldSnapshot& snapshot, const RenderWorkload& workload, RenderCompileContext& context);

    void clear();

    std::span<const MeshDrawCommand> surfaceCommands() const { return surface_commands_; }
    std::span<const MeshDrawCommand> edgeCommands() const { return edge_commands_; }
    std::span<const MeshDrawCommand> highlightSurfaceCommands() const { return highlight_surface_commands_; }
    std::span<const MeshDrawCommand> highlightEdgeCommands() const { return highlight_edge_commands_; }
    const RenderCompilerStats& lastStats() const { return stats_; }

private:
    std::vector<MeshDrawCommand> surface_commands_;
    std::vector<MeshDrawCommand> edge_commands_;
    std::vector<MeshDrawCommand> highlight_surface_commands_;
    std::vector<MeshDrawCommand> highlight_edge_commands_;
    RenderCompilerStats stats_;
};

}  // namespace mulan::engine
