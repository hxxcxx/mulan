/**
 * @file view_config.h
 * @brief ViewContext 初始化配置
 * @author hxxcxx
 * @date 2026-06-01
 *
 * ViewConfig 是 UI 端可控制的引擎初始化参数。
 * 由应用层文档视口填充，传入 ViewContext::init()。
 */

#pragma once

#include <mulan/render/runtime/render_config.h>

#include <cstdint>
#include <string>

namespace mulan::view {

struct ViewConfig {
    engine::GraphicsBackend backend = engine::GraphicsBackend::Vulkan;

    engine::MSAALevel msaa = engine::MSAALevel::x4;

    uint8_t bufferCount = 2;
    bool vsync = true;

    bool enableValidation = true;
    float clearColor[4] = { 97.0f / 255, 101.0f / 255, 118.0f / 255, 1.0f };

    /// IBL（环境光反射）开关。默认关闭：需要 hdrPath 指向一张可读的 HDR 文件。
    /// 开启时启动时一次性烘焙 irradiance/prefilter/BRDF LUT；关闭则完全跳过烘焙。
    bool iblEnabled = false;
    /// HDR 文件路径（相对进程工作目录）。仅 iblEnabled=true 时使用。
    std::string hdrPath = "assets/envmap.hdr";

    /// 由应用壳的平台适配器提供；View 与渲染线程不感知 Qt/Win32/X11。
    engine::NativeWindowHandle window;

    engine::RenderSessionConfig toRenderSessionConfig() const {
        engine::RenderSessionConfig config;
        config.backend = backend;
        config.enableValidation = enableValidation;
        config.surface.window = window;
        config.surface.msaa = msaa;
        config.surface.bufferCount = bufferCount;
        config.surface.vsync = vsync;
        for (int i = 0; i < 4; ++i)
            config.surface.clearColor[i] = clearColor[i];
        return config;
    }
};

}  // namespace mulan::view
