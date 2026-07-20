/**
 * @file present_surface_state.h
 * @brief 定义跨线程发布的不可变窗口呈现状态快照。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <cstdint>

namespace mulan::view::detail {

struct PresentSurfaceState {
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

}  // namespace mulan::view::detail
