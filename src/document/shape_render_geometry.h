/**
 * @file shape_render_geometry.h
 * @brief 将 OCCT 形体转换为当前渲染需要的网格和包围盒。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include <mulan/engine/geometry/mesh.h>
#include <mulan/engine/math/aabb.h>

class TopoDS_Shape;

namespace mulan::document {

struct ShapeRenderGeometry {
    engine::Mesh faceMesh;
    engine::Mesh edgeMesh;
    engine::AABB bounds = engine::AABB::empty();
};

ShapeRenderGeometry buildShapeRenderGeometry(const TopoDS_Shape& shape);

} // namespace mulan::document
