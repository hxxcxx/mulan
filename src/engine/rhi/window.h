/**
 * @file window.h
 * @brief RHI 渲染配置与原生窗口句柄
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include <mulan/core/platform/native_window_handle.h>

#include <cstdint>

namespace mulan::engine {

// ============================================================
// 渲染配置 — 窗口级渲染参数
// ============================================================

struct RenderConfig {
    // --- 背景 ---
    float clearColor[4] = { 97.0f / 255, 101.0f / 255, 118.0f / 255, 1.0f };
    float clearDepth = 1.0f;

    // --- 抗锯齿 ---
    enum class MSAALevel : uint8_t {
        None = 1,
        x2 = 2,
        x4 = 4,
        x8 = 8,
    };
    MSAALevel msaa = MSAALevel::x4;

    // --- 帧缓冲 ---
    uint8_t bufferCount = 2;  // 双缓冲 / 三缓冲
    bool vsync = true;

    // --- 深度缓冲 ---
    bool depthBuffer = true;
    bool stencilBuffer = false;  // pick 模式需要 stencil

    // 便捷
    uint32_t sampleCount() const { return static_cast<uint32_t>(msaa); }
};

}  // namespace mulan::engine
