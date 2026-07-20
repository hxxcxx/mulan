/**
 * @file render_runtime_config.h
 * @brief 渲染执行域内部的 Device 与窗口呈现配置边界
 * @author hxxcxx
 * @date 2026-07-18
 *
 * RenderSessionConfig 进入执行域后拆成 Device 与 Surface 两类状态。非 OpenGL 后端只按
 * Device 配置共享线程，窗口、呈现和清屏参数始终由各 PresentSurface 独立持有。OpenGL 的
 * Context 当前仍由 Device 创建，因此其原生窗口和像素格式参数保留在 Device 配置中，
 * 但 OpenGL 从不参与线程共享。
 */
#pragma once

#include "../render_config.h"

#include "../../../rhi/device.h"

#include <cstdint>

namespace mulan::engine::detail {

struct RenderDeviceConfig {
    GraphicsBackend backend = GraphicsBackend::Vulkan;
    bool enableValidation = true;

    // 仅 OpenGL Context 创建使用；其他后端必须忽略。
    NativeWindowHandle contextWindow;
    bool contextVsync = true;

    static RenderDeviceConfig fromSession(const RenderSessionConfig& config) {
        return RenderDeviceConfig{
            .backend = config.backend,
            .enableValidation = config.enableValidation,
            .contextWindow = config.backend == GraphicsBackend::OpenGL ? config.surface.window : NativeWindowHandle{},
            .contextVsync = config.surface.vsync,
        };
    }

    bool sharesExecutionThreadWith(const RenderDeviceConfig& other) const {
        return backend != GraphicsBackend::OpenGL && backend == other.backend &&
               enableValidation == other.enableValidation;
    }

    DeviceCreateInfo toCreateInfo() const {
        DeviceCreateInfo info;
        info.backend = backend;
        info.enableValidation = enableValidation;
        if (backend == GraphicsBackend::OpenGL) {
            info.window = contextWindow;
            info.renderConfig.vsync = contextVsync;
        }
        return info;
    }
};

}  // namespace mulan::engine::detail
