/**
 * @file Viewport.h
 * @brief Viewport — 吸收 EngineView 的完整视图层
 *
 * 持有：RHIDevice + SwapChain/RenderTarget + Camera + GpuResourceManager
 *       + RenderSystem + RenderGraph + Operator
 *
 * 两种使用模式：
 *   旧：Viewport(world, device) → initRendering() → render()/renderPass()
 *   新：Viewport() → init() → setWorld() → renderFrame()
 *
 * @author hxxcxx
 * @date 2026-05-29 (原始) / 2026-06-01 (重构)
 */

#pragma once

#include "ViewConfig.h"
#include "mulan/engine/rhi/Device.h"
#include "mulan/engine/rhi/SwapChain.h"
#include "mulan/engine/rhi/RenderTarget.h"
#include "mulan/engine/rhi/Buffer.h"
#include "mulan/engine/render/GpuResourceManager.h"
#include "mulan/engine/render/graph/RenderGraph.h"
#include "mulan/engine/scene/camera/Camera.h"
#include "mulan/engine/render/LightEnvironment.h"
#include "mulan/engine/interaction/InputEvent.h"
#include "mulan/engine/interaction/Operator.h"
#include "mulan/engine/interaction/CameraManipulator.h"
#include "mulan/engine/render/viewcube/ViewCubeRenderer.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mulan::world {

class World;
class RenderSystem;

class Viewport {
public:
    // ===== 旧构造（向后兼容）=====
    Viewport(World& world, engine::RHIDevice& device);

    // ===== 新构造（延迟初始化）=====
    Viewport();
    ~Viewport();

    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    // ===== 生命周期（新路径）=====

    /// 窗口模式初始化：创建 Device + SwapChain + Renderer
    bool init(const ViewConfig& config, int width, int height);

    /// 离屏模式初始化：创建 Device + RenderTarget（无窗口）
    bool initOffscreen(int width, int height);

    /// 释放所有 GPU 资源
    void shutdown();

    bool isInitialized() const { return m_initialized; }

    // ===== World 绑定 =====

    void setWorld(World* world);
    World* world() { return m_world; }

    // ===== 渲染 =====

    /// 完整帧循环（逻辑 + 收集 + 绘制 + present）
    /// 新路径使用，替代分别调用 render() + renderPass()
    void renderFrame();

    /// 逻辑 + 渲染收集（旧路径，可在 beginFrame 之前调用）
    void render(float dt);

    /// 绘制到 CommandList（旧路径，外部管理 RenderPass）
    void renderPass(engine::CommandList* cmd);

    void onFrameEnd();
    void resize(int width, int height);

    // ===== 输入 =====

    /// 处理平台无关输入事件，委托给 Operator
    void handleInput(const engine::InputEvent& event);

    // ===== Operator =====

    /// 设置操作器（nullptr 恢复默认 CameraManipulator）
    void setOperator(std::unique_ptr<engine::Operator> op);

    /// 获取当前操作器
    engine::Operator* currentOperator() const { return m_operator.get(); }

    // ===== 离屏 =====

    /// 将 color 纹理回读到 CPU（仅离屏模式）
    bool readbackPixels(std::vector<uint8_t>& pixels);

    // ===== 访问器 =====

    engine::Camera& camera() { return m_camera; }
    const engine::Camera& camera() const { return m_camera; }

    engine::GpuResourceManager& gpu() { return *m_gpu; }
    const engine::GpuResourceManager& gpu() const { return *m_gpu; }

    RenderSystem& renderSystem() { return *m_renderSys; }
    const RenderSystem& renderSystem() const { return *m_renderSys; }

    engine::RenderGraph& renderGraph() { return m_renderGraph; }

    engine::LightEnvironment& lightEnvironment() { return m_lightEnv; }
    const engine::LightEnvironment& lightEnvironment() const { return m_lightEnv; }

    /// 获取离屏渲染目标（nullptr 表示窗口模式）
    engine::RenderTarget* renderTarget() const { return m_renderTarget.get(); }

private:
    bool initRendering(int width, int height);
    bool initSceneRenderer();
    void cleanup();

    // ===== Device（新路径拥有，旧路径借用）=====
    std::shared_ptr<engine::RHIDevice> m_ownedDevice;
    engine::RHIDevice*                 m_device = nullptr;

    // ===== SwapChain / RenderTarget =====
    engine::ResourcePtr<engine::SwapChain>   m_swapchain;
    engine::ResourcePtr<engine::RenderTarget> m_renderTarget;
    engine::ResourcePtr<engine::Buffer>      m_stagingBuffer; // 离屏回读

    // ===== World =====
    World* m_world = nullptr;

    // ===== GPU / Rendering（延迟构造）=====
    std::unique_ptr<engine::GpuResourceManager> m_gpuStorage;
    std::unique_ptr<RenderSystem>               m_renderSysStorage;
    engine::GpuResourceManager*                 m_gpu      = nullptr;
    RenderSystem*                               m_renderSys = nullptr;

    // ===== Camera =====
    engine::Camera m_camera{engine::CameraMode::Trackball};

    // ===== Operator =====
    std::unique_ptr<engine::Operator> m_operator;

    // ===== ViewCube =====
    std::unique_ptr<engine::ViewCubeRenderer> m_viewCubeRenderer;

    // ===== Render Graph =====
    engine::RenderGraph      m_renderGraph;
    engine::LightEnvironment m_lightEnv;

    // ===== 状态 =====
    int  m_width  = 800;
    int  m_height = 600;
    bool m_initialized      = false;
    bool m_renderingInited   = false;
    bool m_worldLogicUpdated = false;
};

} // namespace mulan::world
