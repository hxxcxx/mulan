/**
 * @file window.h
 * @brief RHI 渲染配置与原生窗口句柄
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

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

// ============================================================
// 原生窗口句柄 — 跨平台 tagged union
//
// 只保留 Win32 和 Linux X11 两种，其他平台按需扩展。
// ============================================================

struct NativeWindowHandle {
    enum class Type : uint8_t {
        Unknown = 0,
        Win32,  // Windows: HINSTANCE + HWND
        X11,    // Linux X11: Display* + xcb_connection_t* + Window
    };

    Type type = Type::Unknown;

    union {
        // Win32:  hInstance + hWnd
        struct {
            uintptr_t hInstance;
            uintptr_t hWnd;
        } win32;
        // X11：Xlib Display 供 GLX 使用，XCB connection 供 Vulkan 使用。
        struct {
            uintptr_t display;
            uintptr_t connection;
            uintptr_t window;
        } x11;
    };

    NativeWindowHandle() : type(Type::Unknown), win32{} {}

    [[nodiscard]] bool valid() const noexcept {
        switch (type) {
        case Type::Win32: return win32.hInstance != 0 && win32.hWnd != 0;
        case Type::X11: return x11.display != 0 && x11.connection != 0 && x11.window != 0;
        default: return false;
        }
    }

    // --- 便捷构造 ---

    static NativeWindowHandle makeWin32(uintptr_t hInstance, uintptr_t hWnd) noexcept {
        NativeWindowHandle h;
        h.type = Type::Win32;
        h.win32.hInstance = hInstance;
        h.win32.hWnd = hWnd;
        return h;
    }

    static NativeWindowHandle makeX11(uintptr_t display, uintptr_t connection, uintptr_t window) noexcept {
        NativeWindowHandle h;
        h.type = Type::X11;
        h.x11.display = display;
        h.x11.connection = connection;
        h.x11.window = window;
        return h;
    }
};

}  // namespace mulan::engine
