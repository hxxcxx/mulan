/**
 * @file edge_pass.h
 * @brief 边线渲染 Pass — 消费 MeshDrawCommand 绘制边线
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "render_pass.h"
#include "../render_resource_cache.h"
#include "../mesh_draw_command.h"
#include "../light_environment.h"
#include "../../rhi/device.h"
#include "../../rhi/buffer.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/shader.h"
#include "../../math/math.h"

#include <cstdint>
#include <span>

namespace mulan::engine {

class MaterialCache;

class EdgePass : public RenderPass {
public:
    EdgePass(RHIDevice& device, RenderResourceCache& gpu,
             MaterialCache& matCache,
             const LightEnvironment& lightEnv);

    const char* name() const override { return "EdgePass"; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const PassContext& ctx) override;

    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { commands_ = cmds; }

    PipelineState* pipelineState() const { return pso_.get(); }

private:
    bool loadEdgeShaders();
    bool createEdgePSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const PassContext& ctx);

    RHIDevice&              device_;
    RenderResourceCache&     gpu_;
    MaterialCache&          mat_cache_;
    const LightEnvironment& light_env_;

    std::unique_ptr<Shader>        vs_;
    std::unique_ptr<Shader>        fs_;
    std::unique_ptr<PipelineState> pso_;
    std::unique_ptr<Buffer>        scene_ubo_;
    std::unique_ptr<Buffer>        object_ubo_;
    std::unique_ptr<Buffer>        material_ubo_;

    std::span<const MeshDrawCommand> commands_;
    bool initialized_ = false;
};

} // namespace mulan::engine
