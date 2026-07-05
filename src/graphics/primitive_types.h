/**
 * @file primitive_types.h
 * @brief 定义 graphics 层共享的图元、索引和网格基础类型。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <cstdint>

namespace mulan::graphics {

enum class PrimitiveTopology : uint8_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    LineListAdj,
    LineStripAdj,
    TriangleListAdj,
    TriangleStripAdj,
};

} // namespace mulan::graphics
