/**
 * @file Window.h
 * @brief 平台无关窗口接口，RHI渲染目标抽象
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
    float clearColor[4] = { 97.0f/255, 101.0f/255, 118.0f/255, 1.0f };
    float clearDepth    = 1.0f;

    // --- 抗锯齿 ---
    enum class MSAALevel : uint8_t {
        None   = 1,
        x2     = 2,
        x4     = 4,
        x8     = 8,
    };
    MSAALevel msaa = MSAALevel::x4;

    // --- 帧缓冲 ---
    uint8_t  bufferCount = 2;           // 双缓冲 / 三缓冲
    bool     vsync       = true;

    // --- 深度缓冲 ---
    bool     depthBuffer   = true;
    bool     stencilBuffer = false;     // pick 模式需要 stencil

    // 便捷
    uint32_t sampleCount() const { return static_cast<uint32_t>(msaa); }
};

// ============================================================
// 原生窗口句柄 — 跨平台 tagged union
//
// 只保留 Win32 和 Linux XCB 两种，其他平台按需扩展。
// ============================================================

struct NativeWindowHandle {
    enum class Type : uint8_t {
        Unknown = 0,
        Win32,       // Windows: HINSTANCE + HWND
        XCB,         // Linux X11: xcb_connection_t* + xcb_window_t
    };

    Type type = Type::Unknown;

    union {
        // Win32:  hInstance + hWnd
        struct { uintptr_t hInstance; uintptr_t hWnd; }    win32;
        // XCB:    connection + window
        struct { uintptr_t connection; uintptr_t window; } xcb;
    };

    NativeWindowHandle() : type(Type::Unknown), win32{} {}

    bool valid() const {
        switch (type) {
        case Type::Win32: return win32.hWnd != 0;
        case Type::XCB:   return xcb.connection != 0 && xcb.window != 0;
        default:          return false;
        }
    }

    // --- 便捷构造 ---

    static NativeWindowHandle makeWin32(uintptr_t hInstance, uintptr_t hWnd) {
        NativeWindowHandle h;
        h.type = Type::Win32;
        h.win32.hInstance = hInstance;
        h.win32.hWnd      = hWnd;
        return h;
    }

    static NativeWindowHandle makeXCB(uintptr_t connection, uintptr_t window) {
        NativeWindowHandle h;
        h.type = Type::XCB;
        h.xcb.connection = connection;
        h.xcb.window     = window;
        return h;
    }
};

// ============================================================
// 窗口抽象接口 — RHI 最小依赖
//
// 只描述窗口属性（句柄、尺寸），不负责事件循环。
// 事件循环由外部框架（Qt / Win32 消息泵 / xcb 事件循环）处理。
// ============================================================

class IWindow {
public:
    virtual ~IWindow() = default;

    /// 原生窗口句柄（用于创建 RHI Surface）
    virtual NativeWindowHandle nativeHandle() const = 0;

    /// 窗口客户区尺寸（像素）
    virtual uint32_t width()  const = 0;
    virtual uint32_t height() const = 0;

    // --- 渲染配置 ---
    RenderConfig renderConfig;

    // --- Resize 回调 ---
    using ResizeCallback = void(*)(IWindow& window, uint32_t width, uint32_t height, void* userData);

    void setResizeCallback(ResizeCallback cb, void* userData = nullptr) {
        m_resizeCb     = cb;
        m_resizeCbData = userData;
    }

protected:
    void notifyResize(uint32_t w, uint32_t h) {
        if (m_resizeCb) m_resizeCb(*this, w, h, m_resizeCbData);
    }

    ResizeCallback m_resizeCb     = nullptr;
    void*          m_resizeCbData = nullptr;
};

} // namespace mulan::engine
