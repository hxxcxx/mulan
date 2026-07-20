/**
 * @file render_config.h
 * @brief 定义渲染会话的设备与窗口呈现配置。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <mulan/core/platform/native_window_handle.h>
#include <mulan/graphics/graphics_backend.h>

#include <cstdint>

namespace mulan::engine {

enum class MSAALevel : uint8_t {
    None = 1,
    x2 = 2,
    x4 = 4,
    x8 = 8,
};

struct RenderSurfaceConfig {
    NativeWindowHandle window;
    MSAALevel msaa = MSAALevel::x4;
    uint8_t bufferCount = 2;
    bool vsync = true;
    float clearColor[4] = { 97.0f / 255, 101.0f / 255, 118.0f / 255, 1.0f };
    float clearDepth = 1.0f;
    bool depthBuffer = true;

    uint32_t sampleCount() const { return static_cast<uint32_t>(msaa); }
};

struct RenderSessionConfig {
    GraphicsBackend backend = GraphicsBackend::Vulkan;
    bool enableValidation = true;
    RenderSurfaceConfig surface;
};

}  // namespace mulan::engine
