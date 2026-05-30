/**
 * @file Viewport.h
 * @brief Viewport — 持有 Camera + GpuResourceManager + RenderGraph + RenderSystem
 *
 * 帧循环：updateLogic → RenderSystem → RenderGraph.execute
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "system/RenderSystem.h"
#include "mulan/engine/render/GpuResourceManager.h"
#include "mulan/engine/render/graph/RenderGraph.h"
#include "mulan/engine/scene/camera/Camera.h"
#include "mulan/engine/render/LightEnvironment.h"

#include <cstdint>

namespace mulan::engine {
class RHIDevice;
} // namespace mulan::engine

namespace mulan::world {

class World;

class Viewport {
public:
    Viewport(World& world, engine::RHIDevice& device);

    /// 渲染后端初始化后调用
    bool initRendering(int width, int height);

    /// 逻辑 + 渲染收集（可在 beginFrame 之前调用）
    void render(float dt);

    /// 绘制到指定的 CommandList（外部管理 beginRenderPass / endRenderPass）
    void renderPass(engine::CommandList* cmd);

    void onFrameEnd();
    void resize(int width, int height);

    engine::GpuResourceManager& gpu() { return m_gpu; }
    const engine::GpuResourceManager& gpu() const { return m_gpu; }

    RenderSystem& renderSystem() { return m_renderSys; }
    const RenderSystem& renderSystem() const { return m_renderSys; }

    engine::Camera& camera() { return m_camera; }
    const engine::Camera& camera() const { return m_camera; }

    engine::RenderGraph& renderGraph() { return m_renderGraph; }

private:
    World&                     m_world;
    engine::RHIDevice&         m_device;
    engine::GpuResourceManager m_gpu;
    engine::Camera             m_camera;
    RenderSystem               m_renderSys;
    engine::LightEnvironment   m_lightEnv;
    engine::RenderGraph        m_renderGraph;

    int  m_width  = 800;
    int  m_height = 600;
    bool m_worldLogicUpdated = false;
    bool m_renderingInited   = false;
};

} // namespace mulan::world
