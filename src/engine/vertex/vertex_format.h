#pragma once

#include <mulan/graphics/vertex/vertex_format.h>

namespace mulan::engine {
using graphics::VertexFormat;
using graphics::VertexFormatInfo;
using graphics::getVertexFormatInfo;
using graphics::vertexFormatSize;
using graphics::vertexFormatName;
template<graphics::VertexFormat F>
using VertexFormatTraits = graphics::VertexFormatTraits<F>;
} // namespace mulan::engine
