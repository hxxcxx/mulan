/**
 * @file EdgePass.h
 * @brief 边线渲染 Pass
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "RenderPass.h"
#include "ForwardPass.h"
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
#include <vector>

namespace mulan::engine {

class EdgePass : public RenderPass {
public:
    EdgePass(RHIDevice& device, GpuResourceManager& gpu,
             const Camera& camera, const LightEnvironment& lightEnv);

    const char* name() const override { return "EdgePass"; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const PassContext& ctx) override;

    void setDrawList(const std::vector<DrawBatch>* batches) { m_batches = batches; }

    /// Phase 3: 直接消费 MeshDrawCommand（优先于 DrawBatch）
    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { m_commands = cmds; }

    /// 获取 PipelineState（供 RenderSystem 设置 PSO）
    PipelineState* pipelineState() const { return m_pso.get(); }

private:
    bool loadEdgeShaders();
    void createEdgePSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const PassContext& ctx);
    void uploadObjectUBO(const Mat4& world, const PassContext& ctx);
    void drawBatch(const DrawBatch& batch, const PassContext& ctx);

    RHIDevice&              m_device;
    GpuResourceManager&     m_gpu;
    const Camera&           m_camera;
    const LightEnvironment& m_lightEnv;

    ResourcePtr<Shader>        m_vs;
    ResourcePtr<Shader>        m_fs;
    ResourcePtr<PipelineState> m_pso;
    ResourcePtr<Buffer>        m_sceneUbo;
    ResourcePtr<Buffer>        m_objectUbo;

    const std::vector<DrawBatch>* m_batches = nullptr;
    std::span<const MeshDrawCommand> m_commands;
    bool m_initialized = false;
};

} // namespace mulan::engine
