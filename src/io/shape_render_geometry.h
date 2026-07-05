/**
 * @file shape_render_geometry.h
 * @brief 将 OCCT 形体转换为当前渲染需要的网格和包围盒。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

class TopoDS_Shape;

namespace mulan::io {

struct ShapeRenderGeometry {
    graphics::Mesh solidMesh;
    graphics::Mesh wireMesh;
    math::AABB3 bounds = math::AABB3::empty();
};

ShapeRenderGeometry buildShapeRenderGeometry(const TopoDS_Shape& shape);

}  // namespace mulan::io
