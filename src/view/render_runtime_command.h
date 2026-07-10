/**
 * @file render_runtime_command.h
 * @brief RenderRuntimeCommand 定义渲染运行时的生命周期命令协议。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 命令只含值数据，不捕获 UI、Document 或 GPU 资源指针。当前由同步运行时立即执行，
 * 后续可原样作为渲染线程的有序控制消息。
 */

#pragma once

#include "render_surface.h"

#include <mulan/engine/render/frontend/render_capture.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace mulan::view {

struct ResizeSurfaceCommand {
    int width = 0;
    int height = 0;
};

struct EnableIblCommand {
    std::string hdrPath;
};

struct ConfigureCaptureSurfaceCommand {
    engine::RenderCaptureDesc capture;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ConfigureOffscreenSurfaceCommand {
    RenderSurfaceDesc surface;
};

struct ReadbackPixelsCommand {};
struct ClearAssetResourcesCommand {};
struct ShutdownRendererCommand {};

using RenderRuntimeCommand = std::variant<ResizeSurfaceCommand, EnableIblCommand, ConfigureCaptureSurfaceCommand,
                                          ConfigureOffscreenSurfaceCommand, ReadbackPixelsCommand,
                                          ClearAssetResourcesCommand, ShutdownRendererCommand>;

struct RenderRuntimeCommandResult {
    bool succeeded = false;
    std::vector<uint8_t> pixels;
};

}  // namespace mulan::view
