/**
 * @file forward_pass.h
 * @brief 前向渲染 Pass — 消费 MeshDrawCommand 绘制固体面
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "render_pass.h"
#include "../gpu_resource_manager.h"
#include "../mesh_draw_command.h"
#include "../light_environment.h"
#include "../../rhi/device.h"
#include "../../rhi/buffer.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/shader.h"
#include "../../math/math.h"
#include "../../scene/camera/camera.h"

#include <cstdint>
#include <span>

namespace mulan::engine {

class MaterialCache;

class ForwardPass : public RenderPass {
public:
    ForwardPass(RHIDevice& device, GpuResourceManager& gpu,
                MaterialCache& matCache,
                const Camera& camera, const LightEnvironment& lightEnv);

    const char* name() const override { return "ForwardPass"; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const PassContext& ctx) override;

    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { commands_ = cmds; }

    PipelineState* pipelineState() const { return pso_.get(); }

private:
    bool loadSolidShaders();
    void createSolidPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const PassContext& ctx);

    RHIDevice&              device_;
    GpuResourceManager&     gpu_;
    MaterialCache&          mat_cache_;
    const Camera&           camera_;
    const LightEnvironment& light_env_;

    ResourcePtr<Shader>        vs_;
    ResourcePtr<Shader>        fs_;
    ResourcePtr<PipelineState> pso_;
    ResourcePtr<Buffer>        scene_ubo_;   // set=0, binding=0
    ResourcePtr<Buffer>        object_ubo_;  // set=0, binding=1
    ResourcePtr<Buffer>        material_ubo_; // set=0, binding=2

    std::span<const MeshDrawCommand> commands_;
    bool initialized_ = false;
};

} // namespace mulan::engine
