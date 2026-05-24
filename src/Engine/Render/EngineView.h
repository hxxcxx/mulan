/**
 * @file EngineView.h
 * @brief 平台无关的引擎视图 — Device + Camera + Operator 的整合层
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 职责：
 *  - GPU 设备 / SwapChain / RenderTarget 生命周期
 *  - 帧循环调度（收集 → 排序 → 渲染 → 提交）
 *  - 输入 → Operator → Camera
 *
 * 不负责：Shader/PSO/UBO 管理（归 SceneRenderer）
 */

#pragma once

#include "../RHI/Device.h"
#include "../RHI/SwapChain.h"
#include "../RHI/RenderTarget.h"
#include "../RHI/Buffer.h"
#include "../Scene/Camera/Camera.h"
#include "../Scene/Scene.h"
#include "../Scene/CullVisitor.h"
#include "../Interaction/InputEvent.h"
#include "../Interaction/Operator.h"
#include "../Interaction/CameraManipulator.h"
#include "../Window.h"
#include "SceneRenderer.h"
#include "RenderGeometry.h"
#include "../Math/Math.h"
#include "../Math/AABB.h"

#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace MulanGeo::engine {

// ============================================================
// ViewConfig — UI 端可控制的引擎初始化配置
//
// 由 DocWidget / WASM shell 等上层填充，传入 EngineView::init。
// EngineView 内部据此创建 NativeWindowHandle / DeviceCreateInfo。
// ============================================================

struct ViewConfig {
    // --- 渲染后端 ---
    GraphicsBackend backend = GraphicsBackend::Vulkan;

    // --- 抗锯齿 ---
    RenderConfig::MSAALevel msaa = RenderConfig::MSAALevel::x4;

    // --- 帧缓冲 ---
    uint8_t  bufferCount = 2;           // 双缓冲 / 三缓冲
    bool     vsync       = true;

    // --- 深度缓冲 ---
    bool     depthBuffer   = true;
    bool     stencilBuffer = false;

    // --- 调试 ---
    bool     enableValidation = true;

    // --- 背景色 ---
    float    clearColor[4] = { 97.0f/255, 101.0f/255, 118.0f/255, 1.0f };

    // --- 原生窗口信息（平台相关）---
#ifdef _WIN32
    uintptr_t hInstance = 0;
    uintptr_t hWnd      = 0;
#else
    uintptr_t displayConnection = 0;
    uintptr_t windowHandle      = 0;
#endif

    // --- 便捷：转换为 RenderConfig ---
    RenderConfig toRenderConfig() const {
        RenderConfig rc;
        rc.msaa           = msaa;
        rc.bufferCount    = bufferCount;
        rc.vsync          = vsync;
        rc.depthBuffer    = depthBuffer;
        rc.stencilBuffer  = stencilBuffer;
        for (int i = 0; i < 4; ++i) rc.clearColor[i] = clearColor[i];
        return rc;
    }

    // --- 便捷：转换为 NativeWindowHandle ---
    NativeWindowHandle toNativeWindowHandle() const {
#ifdef _WIN32
        if (hWnd) return NativeWindowHandle::makeWin32(hInstance, hWnd);
#else
        if (displayConnection && windowHandle)
            return NativeWindowHandle::makeXCB(displayConnection, windowHandle);
#endif
        return {};
    }
};

// ============================================================
// EngineView — 引擎视图
// ============================================================

class EngineView {
public:
    EngineView();
    ~EngineView();

    EngineView(const EngineView&) = delete;
    EngineView& operator=(const EngineView&) = delete;

    // --- 生命周期 ---

    /// 初始化（窗口第一次显示时调用）
    bool init(const ViewConfig& config, int width, int height);

    /// 离屏初始化（无窗口，渲染到纹理）
    bool initOffscreen(int width, int height);

    /// 窗口大小改变
    void resize(int width, int height);

    /// 释放所有 GPU 资源
    void shutdown();

    bool isInitialized() const { return m_initialized; }

    // --- 渲染 ---

    /// 渲染一帧
    void renderFrame();

    /// 将 color 纹理回读到 CPU（仅离屏模式）
    /// @param pixels 输出 RGBA 像素数据（自动 resize）
    /// @return true=成功
    bool readbackPixels(std::vector<uint8_t>& pixels);

    // --- 输入 ---

    /// 处理平台无关输入事件
    void handleInput(const InputEvent& event);

    // --- Operator 管理 ---

    /// 设置当前操作器（nullptr 恢复默认 CameraManipulator）
    void setOperator(std::unique_ptr<Operator> op);

    /// 获取当前操作器
    Operator* currentOperator() const { return m_operator.get(); }

    /// 获取离屏渲染目标（nullptr 表示非离屏模式）
    RenderTarget* renderTarget() const { return m_renderTarget.get(); }

    // --- Camera ---

    Camera&       camera()       { return m_camera; }
    const Camera& camera() const { return m_camera; }

    // --- 光照环境 ---

    LightEnvironment&       lightEnvironment()       { return m_lightEnv; }
    const LightEnvironment& lightEnvironment() const { return m_lightEnv; }

    // --- 场景 ---

    /// 设置渲染场景（接管每帧收集逻辑：updateWorldTransforms + CullVisitor）
    void setScene(Scene* scene);

    /// 清除场景引用
    void clearScene();
private:
    bool initSceneRenderer();
    void cleanup();

    // --- RHI 资源 ---
    std::shared_ptr<RHIDevice>   m_device;
    ResourcePtr<SwapChain>      m_swapchain;
    ResourcePtr<RenderTarget>   m_renderTarget;
    ResourcePtr<Buffer>         m_stagingBuffer;

    // --- Camera & Interaction ---

    Camera                              m_camera;
    std::unique_ptr<Operator>           m_operator;

    // --- Renderer ---
    std::unique_ptr<SceneRenderer>      m_sceneRenderer;
    RenderQueue                         m_renderQueue;
    Scene*                              m_scene = nullptr;
    LightEnvironment                    m_lightEnv;

    // --- 状态 ---

    int  m_width       = 0;
    int  m_height      = 0;
    bool m_initialized = false;
};

} // namespace MulanGeo::Engine
