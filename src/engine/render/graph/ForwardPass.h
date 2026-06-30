/**
 * @file ForwardPass.h
 * @brief 前向渲染 Pass — 消费 MeshDrawCommand 绘制固体面
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "RenderPass.h"
#include "../GpuResourceManager.h"
#include "../MeshDrawCommand.h"
#include "../LightEnvironment.h"
#include "../../rhi/Device.h"
#include "../../rhi/Buffer.h"
#include "../../rhi/PipelineState.h"
#include "../../rhi/Shader.h"
#include "../../math/Math.h"
#include "../../scene/camera/Camera.h"

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

    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { m_commands = cmds; }

    PipelineState* pipelineState() const { return m_pso.get(); }

private:
    bool loadSolidShaders();
    void createSolidPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const PassContext& ctx);

    RHIDevice&              m_device;
    GpuResourceManager&     m_gpu;
    MaterialCache&          m_matCache;
    const Camera&           m_camera;
    const LightEnvironment& m_lightEnv;

    ResourcePtr<Shader>        m_vs;
    ResourcePtr<Shader>        m_fs;
    ResourcePtr<PipelineState> m_pso;
    ResourcePtr<Buffer>        m_sceneUbo;   // set=0, binding=0
    ResourcePtr<Buffer>        m_objectUbo;  // set=0, binding=1
    ResourcePtr<Buffer>        m_materialUbo; // set=0, binding=2

    std::span<const MeshDrawCommand> m_commands;
    bool m_initialized = false;
};

} // namespace mulan::engine
