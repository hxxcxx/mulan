/**
 * @file draw_command_builder.h
 * @brief DrawCommandBuilder 将 RenderScene 转换为可提交的绘制命令。
 * @date 2026-07-03
 */
#pragma once

#include "mulan/engine/render/mesh_draw_command.h"

#include <span>
#include <vector>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::engine {
class RenderResourceCache;
class PipelineState;
}

namespace mulan::render_scene {
class RenderScene;
}

namespace mulan::view {

class DrawCommandBuilder {
public:
    void setScene(const render_scene::RenderScene* scene,
                  const asset::AssetLibrary* assets);

    void rebuild(engine::RenderResourceCache& resources,
                 engine::PipelineState* facePso,
                 engine::PipelineState* edgePso);

    void clear();

    std::span<const engine::MeshDrawCommand> faceCommands() const { return face_cmds_; }
    std::span<const engine::MeshDrawCommand> edgeCommands() const { return edge_cmds_; }

private:
    const render_scene::RenderScene* scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;

    std::vector<engine::MeshDrawCommand> face_cmds_;
    std::vector<engine::MeshDrawCommand> edge_cmds_;
};

} // namespace mulan::view
