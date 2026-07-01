/**
 * @file viewport.h
 * @brief Viewport — 完整的视图层
 *
 * 持有：RHIDevice + SwapChain/RenderTarget + Camera + GpuResourceManager
 *       + RenderSystem + RenderGraph + Operator
 *
 * 使用: Viewport() → init() → setWorld() → renderFrame()
 */

#pragma once

#include "view_config.h"
#include "mulan/engine/rhi/device.h"
#include "mulan/engine/rhi/swap_chain.h"
#include "mulan/engine/rhi/render_target.h"
#include "mulan/engine/rhi/buffer.h"
#include "mulan/engine/render/gpu_resource_manager.h"
#include "mulan/engine/render/graph/render_graph.h"
#include "mulan/engine/scene/camera/camera.h"
#include "mulan/engine/render/light_environment.h"
#include "mulan/engine/interaction/input_event.h"
#include "mulan/engine/interaction/operator.h"
#include "mulan/engine/interaction/camera_manipulator.h"
#include "mulan/engine/render/viewcube/view_cube_renderer.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mulan::world {

class World;
class RenderSystem;

class Viewport {
public:
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

    bool isInitialized() const { return initialized_; }

    // ===== World 绑定 =====

    void setWorld(World* world);
    World* world() { return world_; }

    // ===== 渲染 =====

    /// 完整帧循环（逻辑 + 收集 + 绘制 + present）
    void renderFrame();

    void onFrameEnd();
    void resize(int width, int height);

    // ===== 输入 =====

    /// 处理平台无关输入事件，委托给当前 active Operator（栈顶或默认）
    void handleInput(const engine::InputEvent& event);

    // ===== Operator（LIFO 栈）=====

    /// 压入模态交互 Operator。挂起栈顶操作器，激活新 Operator。
    /// 新 Operator 的 onFinish 回调会被自动连接到 popOperator()，
    /// 因此子类只需调用 finish() 即可自动恢复下层操作器。
    void pushOperator(std::unique_ptr<engine::Operator> op);

    /// 弹出栈顶模态交互 Operator，恢复下层（或默认 CameraManipulator）。
    void popOperator();

    /// 当前 active 操作器（栈顶；栈空时为默认 CameraManipulator）。
    engine::Operator* activeOperator() const;

    /// 默认相机操控操作器（栈底，始终存在）
    engine::Operator* defaultOperator() const { return default_op_.get(); }

    // ===== 离屏 =====

    /// 将 color 纹理回读到 CPU（仅离屏模式）
    bool readbackPixels(std::vector<uint8_t>& pixels);

    // ===== 访问器 =====

    engine::Camera& camera() { return camera_; }
    const engine::Camera& camera() const { return camera_; }

    engine::GpuResourceManager& gpu() { return *gpu_; }
    const engine::GpuResourceManager& gpu() const { return *gpu_; }

    RenderSystem& renderSystem() { return *render_sys_; }
    const RenderSystem& renderSystem() const { return *render_sys_; }

    engine::RenderGraph& renderGraph() { return render_graph_; }

    engine::LightEnvironment& lightEnvironment() { return light_env_; }
    const engine::LightEnvironment& lightEnvironment() const { return light_env_; }

    /// 获取离屏渲染目标（nullptr 表示窗口模式）
    engine::RenderTarget* renderTarget() const { return render_target_.get(); }

private:
    bool initRendering(int width, int height);
    bool initSceneRenderer();
    void cleanup();

    // ===== Device =====
    std::shared_ptr<engine::RHIDevice> device_;

    // ===== SwapChain / RenderTarget =====
    std::unique_ptr<engine::SwapChain>    swapchain_;
    std::unique_ptr<engine::RenderTarget> render_target_;
    std::unique_ptr<engine::Buffer>       staging_buffer_; // 离屏回读

    // ===== World =====
    World* world_ = nullptr;

    // ===== GPU / Rendering（延迟构造）=====
    std::unique_ptr<engine::GpuResourceManager> gpu_storage_;
    std::unique_ptr<RenderSystem>               render_sys_storage_;
    engine::GpuResourceManager*                 gpu_      = nullptr;
    RenderSystem*                               render_sys_ = nullptr;

    // ===== Camera =====
    engine::Camera camera_{engine::CameraMode::Trackball};

    // ===== Operator =====
    std::unique_ptr<engine::Operator>              default_op_;   // 栈底：CameraManipulator
    std::vector<std::unique_ptr<engine::Operator>> op_stack_;     // 模态交互栈（LIFO）

    // ===== ViewCube =====
    std::unique_ptr<engine::ViewCubeRenderer> view_cube_renderer_;

    // ===== Render Graph =====
    engine::RenderGraph      render_graph_;
    engine::LightEnvironment light_env_;

    // ===== 状态 =====
    int  width_  = 800;
    int  height_ = 600;
    bool initialized_      = false;
    bool world_logic_updated_ = false;
};

} // namespace mulan::world
