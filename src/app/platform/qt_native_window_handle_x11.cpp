/**
 * @file qt_native_window_handle_x11.cpp
 * @brief Qt/X11 原生窗口句柄适配
 * @author hxxcxx
 * @date 2026-07-18
 */

#include "qt_native_window_handle.h"

#include <QApplication>
#include <QWidget>
#include <QtGui/qguiapplication_platform.h>

namespace mulan::app {

engine::NativeWindowHandle nativeWindowHandle(QWidget& widget) {
    if (!qApp)
        return {};

    auto* x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->display() || !x11->connection())
        return {};

    const uintptr_t window = static_cast<uintptr_t>(widget.winId());
    if (window == 0)
        return {};

    return engine::NativeWindowHandle::makeX11(reinterpret_cast<uintptr_t>(x11->display()),
                                               reinterpret_cast<uintptr_t>(x11->connection()), window);
}

}  // namespace mulan::app
