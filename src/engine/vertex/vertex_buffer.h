#pragma once

#include <mulan/graphics/vertex/vertex_buffer.h>

namespace mulan::engine {
using graphics::packColor;
using graphics::packColorF;
template<graphics::VertexFormat F>
using VertexElement = graphics::VertexElement<F>;
using graphics::VertexBufferView;
using graphics::VertexBufferBuilder;
} // namespace mulan::engine
