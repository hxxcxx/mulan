/**
 * @file qt_native_window_handle.h
 * @brief 将 Qt 窗口转换为 RHI 原生窗口句柄
 * @author hxxcxx
 * @date 2026-07-18
 */

#pragma once

#include <mulan/rhi/window.h>

class QWidget;

namespace mulan::app {

/**
 * @brief 提取 Qt 窗口所属平台的原生句柄
 *
 * Qt 保持所有原生对象的所有权；返回值仅用于其窗口生命周期内的渲染设备与交换表面。
 */
engine::NativeWindowHandle nativeWindowHandle(QWidget& widget);

}  // namespace mulan::app
