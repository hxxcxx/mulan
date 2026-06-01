/**
 * @file ViewConfig.h
 * @brief Viewport 初始化配置 — 从 EngineView::ViewConfig 迁移而来
 * @date 2026-06-01
 *
 * ViewConfig 是 UI 端可控制的引擎初始化参数。
 * 由 DocWidget / WASM shell 等上层填充，传入 Viewport::init()。
 */

#pragma once

#include "mulan/engine/rhi/Device.h"
#include "mulan/engine/Window.h"

#include <cstdint>

namespace mulan::world {

struct ViewConfig {
    // --- 渲染后端 ---
    engine::GraphicsBackend backend = engine::GraphicsBackend::Vulkan;

    // --- 抗锯齿 ---
    engine::RenderConfig::MSAALevel msaa = engine::RenderConfig::MSAALevel::x4;

    // --- 帧缓冲 ---
    uint8_t  bufferCount = 2;
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

    // --- 便捷转换 ---

    engine::RenderConfig toRenderConfig() const {
        engine::RenderConfig rc;
        rc.msaa           = msaa;
        rc.bufferCount    = bufferCount;
        rc.vsync          = vsync;
        rc.depthBuffer    = depthBuffer;
        rc.stencilBuffer  = stencilBuffer;
        for (int i = 0; i < 4; ++i) rc.clearColor[i] = clearColor[i];
        return rc;
    }

    engine::NativeWindowHandle toNativeWindowHandle() const {
#ifdef _WIN32
        if (hWnd) return engine::NativeWindowHandle::makeWin32(hInstance, hWnd);
#else
        if (displayConnection && windowHandle)
            return engine::NativeWindowHandle::makeXCB(displayConnection, windowHandle);
#endif
        return {};
    }
};

} // namespace mulan::world
