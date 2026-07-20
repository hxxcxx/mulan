/**
 * @file graphics_backend.h
 * @brief 定义应用、渲染运行时与 RHI 共同使用的图形后端标识。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <cstdint>

namespace mulan::engine {

enum class GraphicsBackend : uint8_t { OpenGL, Vulkan, D3D11, D3D12 };

}  // namespace mulan::engine
