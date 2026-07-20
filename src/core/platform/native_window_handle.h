/**
 * @file native_window_handle.h
 * @brief 定义跨平台原生窗口的非拥有句柄。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <cstdint>

namespace mulan::engine {

struct NativeWindowHandle {
    enum class Type : uint8_t {
        Unknown = 0,
        Win32,
        X11,
    };

    Type type = Type::Unknown;

    union {
        struct {
            uintptr_t hInstance;
            uintptr_t hWnd;
        } win32;
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

    static NativeWindowHandle makeWin32(uintptr_t hInstance, uintptr_t hWnd) noexcept {
        NativeWindowHandle handle;
        handle.type = Type::Win32;
        handle.win32.hInstance = hInstance;
        handle.win32.hWnd = hWnd;
        return handle;
    }

    static NativeWindowHandle makeX11(uintptr_t display, uintptr_t connection, uintptr_t window) noexcept {
        NativeWindowHandle handle;
        handle.type = Type::X11;
        handle.x11.display = display;
        handle.x11.connection = connection;
        handle.x11.window = window;
        return handle;
    }
};

}  // namespace mulan::engine
