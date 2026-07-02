/**
 * @file view_config.h
 * @brief ViewContext 初始化配置
 * @date 2026-06-01
 *
 * ViewConfig 是 UI 端可控制的引擎初始化参数。
 * 由 DocWidget 等上层填充，传入 ViewContext::init()。
 */

#pragma once

#include "mulan/engine/rhi/device.h"
#include "mulan/engine/window.h"

#include <cstdint>

namespace mulan::view {

struct ViewConfig {
    engine::GraphicsBackend backend = engine::GraphicsBackend::Vulkan;

    engine::RenderConfig::MSAALevel msaa = engine::RenderConfig::MSAALevel::x4;

    uint8_t bufferCount = 2;
    bool vsync = true;

    bool depthBuffer = true;
    bool stencilBuffer = false;

    bool enableValidation = true;

    float clearColor[4] = {97.0f / 255, 101.0f / 255, 118.0f / 255, 1.0f};

#ifdef _WIN32
    uintptr_t hInstance = 0;
    uintptr_t hWnd = 0;
#else
    uintptr_t displayConnection = 0;
    uintptr_t windowHandle = 0;
#endif

    engine::RenderConfig toRenderConfig() const {
        engine::RenderConfig rc;
        rc.msaa = msaa;
        rc.bufferCount = bufferCount;
        rc.vsync = vsync;
        rc.depthBuffer = depthBuffer;
        rc.stencilBuffer = stencilBuffer;
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

} // namespace mulan::view
