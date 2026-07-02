/**
 * @file forward_pass.h
 * @brief 前向渲染 Pass — 消费 MeshDrawCommand 绘制固体面
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

class ForwardPass : public RenderPass {
public:
    ForwardPass(RHIDevice& device, RenderResourceCache& gpu,
                MaterialCache& matCache,
                const LightEnvironment& lightEnv);

    const char* name() const override { return "ForwardPass"; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const PassContext& ctx) override;

    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { commands_ = cmds; }

    PipelineState* pipelineState() const { return pso_.get(); }

private:
    bool loadSolidShaders();
    bool createSolidPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const PassContext& ctx);

    RHIDevice&              device_;
    RenderResourceCache&     gpu_;
    MaterialCache&          mat_cache_;
    const LightEnvironment& light_env_;

    std::unique_ptr<Shader>        vs_;
    std::unique_ptr<Shader>        fs_;
    std::unique_ptr<PipelineState> pso_;
    std::unique_ptr<Buffer>        scene_ubo_;   // set=0, binding=0
    std::unique_ptr<Buffer>        object_ubo_;  // set=0, binding=1
    std::unique_ptr<Buffer>        material_ubo_; // set=0, binding=2

    std::span<const MeshDrawCommand> commands_;
    bool initialized_ = false;
};

} // namespace mulan::engine
