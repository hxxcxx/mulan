/**
 * @file render_surface_state.h
 * @brief 定义跨线程发布的不可变渲染表面状态快照。
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include <cstdint>

namespace mulan::view::detail {

struct RenderSurfaceState {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t generation = 0;
    bool valid = false;
};

}  // namespace mulan::view::detail
