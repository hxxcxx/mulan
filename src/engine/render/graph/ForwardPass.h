/**
 * @file ForwardPass.h
 * @brief 前向渲染 Pass — 按 DrawBatch 消费 GpuResourceManager → 绘制
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "RenderPass.h"
#include "../GpuResourceManager.h"
#include "../LightEnvironment.h"
#include "../../rhi/Device.h"
#include "../../rhi/Buffer.h"
#include "../../rhi/PipelineState.h"
#include "../../rhi/Shader.h"
#include "../../math/Math.h"
#include "../../scene/camera/Camera.h"

#include <cstdint>
#include <vector>

namespace mulan::engine {

/// 按材质分组的绘制批次（RenderSystem 产出）
struct DrawBatch {
    uint16_t              materialId = 0;
    std::vector<uint64_t> keys;   // GpuResourceManager 查找 key
};

class ForwardPass : public RenderPass {
public:
    ForwardPass(RHIDevice& device, GpuResourceManager& gpu,
                const Camera& camera, const LightEnvironment& lightEnv);

    const char* name() const override { return "ForwardPass"; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const PassContext& ctx) override;

    /// 每帧调用：设置当前要绘制的批次
    void setDrawList(const std::vector<DrawBatch>* batches) { m_batches = batches; }

private:
    bool loadSolidShaders();
    void createSolidPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const PassContext& ctx);
    void drawBatch(const DrawBatch& batch, const PassContext& ctx);

    RHIDevice&              m_device;
    GpuResourceManager&     m_gpu;
    const Camera&           m_camera;
    const LightEnvironment& m_lightEnv;

    ResourcePtr<Shader>        m_vs;
    ResourcePtr<Shader>        m_fs;
    ResourcePtr<PipelineState> m_pso;
    ResourcePtr<Buffer>        m_sceneUbo;

    const std::vector<DrawBatch>* m_batches = nullptr;
    bool m_initialized = false;
};

} // namespace mulan::engine
