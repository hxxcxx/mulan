/**
 * @file display_mode.h
 * @brief 定义视图对外可选的表面与边线显示模式。
 * @author hxxcxx
 * @date 2026-07-21
 */

#pragma once

#include <cstdint>

namespace mulan::engine {

enum class DisplayMode : uint8_t {
    Shaded,
    ShadedWithEdges,
    Wireframe,
};

}  // namespace mulan::engine
