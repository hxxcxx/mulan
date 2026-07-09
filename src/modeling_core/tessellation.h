/**
 * @file tessellation.h
 * @brief 中立 tessellation 请求/结果类型。零内核依赖。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "modeling_core_export.h"

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

namespace mulan::modeling {

/// tessellation 精度与输出控制。中性参数，与具体内核无关。
struct TessellationOptions {
    double linearDeflection = 0.001;
    double angularDeflection = 0.5;
    bool includeEdges = true;
    bool includeNormals = true;
};

/// tessellation 产物：实体填充网格（三角）+ 线框网格（线段）+ 包围盒。
struct TessellatedGeometry {
    graphics::Mesh solidMesh;  // PrimitiveTopology::TriangleList
    graphics::Mesh wireMesh;   // PrimitiveTopology::LineList
    math::AABB3 bounds = math::AABB3::empty();
};

}  // namespace mulan::modeling
