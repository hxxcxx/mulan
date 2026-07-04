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
class MaterialCache;
class PipelineState;
class RenderResourceCache;
class TextureCache;
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
                 engine::PipelineState* edgePso,
                 engine::TextureCache& textureCache,
                 engine::MaterialCache& matCache);

    void clear();

    std::span<const engine::MeshDrawCommand> solidCommands() const { return solid_cmds_; }
    std::span<const engine::MeshDrawCommand> wireCommands() const { return wire_cmds_; }

private:
    const render_scene::RenderScene* scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;

    std::vector<engine::MeshDrawCommand> solid_cmds_;
    std::vector<engine::MeshDrawCommand> wire_cmds_;
};

} // namespace mulan::view