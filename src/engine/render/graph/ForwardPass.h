/**
 * @file ForwardPass.h
 * @brief 前向渲染 Pass — 消费 GpuResourceManager → 绘制不透明面
 *
 * 不依赖 SceneRenderer，独立管理 shader / PSO / UBO。
 * Phase 3 仅固体渲染，EdgePass/Culling 在 Phase 4。
 *
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

class ForwardPass : public RenderPass {
public:
    struct DrawItem {
        uint64_t key;
        Mat4     worldTransform;
        Vec3     color;
    };

    ForwardPass(RHIDevice& device, GpuResourceManager& gpu,
                const Camera& camera, const LightEnvironment& lightEnv);

    const char* name() const override { return "ForwardPass"; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const PassContext& ctx) override;

    std::vector<DrawItem>& items() { return m_items; }
    const std::vector<DrawItem>& items() const { return m_items; }

private:
    bool loadSolidShaders();
    void createSolidPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const PassContext& ctx);

    RHIDevice&              m_device;
    GpuResourceManager&     m_gpu;
    const Camera&           m_camera;
    const LightEnvironment& m_lightEnv;

    ResourcePtr<Shader>        m_vs;
    ResourcePtr<Shader>        m_fs;
    ResourcePtr<PipelineState> m_pso;
    ResourcePtr<Buffer>        m_sceneUbo;

    std::vector<DrawItem> m_items;
    bool m_initialized = false;
};

} // namespace mulan::engine
