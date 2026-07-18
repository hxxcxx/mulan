/**
 * @file qt_native_window_handle_win32.cpp
 * @brief Qt/Win32 原生窗口句柄适配
 * @author hxxcxx
 * @date 2026-07-18
 */

#include "qt_native_window_handle.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <QWidget>

namespace mulan::app {

engine::NativeWindowHandle nativeWindowHandle(QWidget& widget) {
    const uintptr_t window = static_cast<uintptr_t>(widget.winId());
    const auto instance = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (window == 0 || instance == 0)
        return {};
    return engine::NativeWindowHandle::makeWin32(instance, window);
}

}  // namespace mulan::app
