#pragma once

#include "render_compile_context.h"
#include "../frontend/render_workload.h"
#include "../mesh_draw_command.h"

#include <span>
#include <vector>

namespace mulan::engine {

class RenderCompiler {
public:
    void compile(const RenderWorldSnapshot& snapshot,
                 const RenderWorkload& workload,
                 RenderCompileContext& context);

    void clear();

    std::span<const MeshDrawCommand> surfaceCommands() const { return surface_commands_; }
    std::span<const MeshDrawCommand> edgeCommands() const { return edge_commands_; }

private:
    std::vector<MeshDrawCommand> surface_commands_;
    std::vector<MeshDrawCommand> edge_commands_;
};

} // namespace mulan::engine
