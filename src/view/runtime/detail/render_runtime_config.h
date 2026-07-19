/**
 * @file render_runtime_config.h
 * @brief 渲染执行域内部的 Device 与 Surface 配置边界
 * @author hxxcxx
 * @date 2026-07-18
 *
 * ViewConfig 是 UI 侧的一体化配置；进入渲染执行域后必须拆分。非 OpenGL 后端只按
 * Device 配置共享线程，窗口、呈现和清屏参数始终由各 Surface 独立持有。OpenGL 的
 * Context 当前仍由 Device 创建，因此其原生窗口和像素格式参数保留在 Device 配置中，
 * 但 OpenGL 从不参与线程共享。
 */
#pragma once

#include "../../core/view_config.h"

#include <cstdint>

namespace mulan::view::detail {

struct RenderDeviceConfig {
    engine::GraphicsBackend backend = engine::GraphicsBackend::Vulkan;
    bool enableValidation = true;

    // 仅 OpenGL Context 创建使用；其他后端必须忽略。
    engine::NativeWindowHandle contextWindow;
    bool contextVsync = true;

    static RenderDeviceConfig fromView(const ViewConfig& config) {
        return RenderDeviceConfig{
            .backend = config.backend,
            .enableValidation = config.enableValidation,
            .contextWindow =
                    config.backend == engine::GraphicsBackend::OpenGL ? config.window : engine::NativeWindowHandle{},
            .contextVsync = config.vsync,
        };
    }

    bool sharesExecutionThreadWith(const RenderDeviceConfig& other) const {
        return backend != engine::GraphicsBackend::OpenGL && backend == other.backend &&
               enableValidation == other.enableValidation;
    }

    engine::DeviceCreateInfo toCreateInfo() const {
        engine::DeviceCreateInfo info;
        info.backend = backend;
        info.enableValidation = enableValidation;
        if (backend == engine::GraphicsBackend::OpenGL) {
            info.window = contextWindow;
            info.renderConfig.vsync = contextVsync;
        }
        return info;
    }
};

struct RenderSurfaceConfig {
    engine::NativeWindowHandle window;
    engine::RenderConfig render;

    static RenderSurfaceConfig fromView(const ViewConfig& config) {
        RenderSurfaceConfig result{
            .window = config.window,
            .render = config.toRenderConfig(),
        };
        if (result.render.bufferCount == 0) {
            result.render.bufferCount = 2;
        }
        return result;
    }
};

}  // namespace mulan::view::detail
