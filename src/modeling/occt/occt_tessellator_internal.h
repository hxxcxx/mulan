/**
 * @file occt_tessellator_internal.h
 * @brief modeling_occt 内部：TopoDS_Shape -> TessellatedGeometry 离散算法入口。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * 模块内部头（带 TopoDS_Shape），禁止泄漏到 modeling_occt 之外。
 */
#pragma once

#include <mulan/core/result/error.h>
#include <mulan/modeling/core/tessellation.h>

#include <TopoDS_Shape.hxx>

namespace mulan::modeling::detail {

/// 离散 TopoDS_Shape 为 mulan 中立网格。算法搬自原 io/shape_render_geometry。
///
/// 流程：bounds(BRepBndLib) -> faceMesh(BRepMesh_IncrementalMesh + Poly_Triangulation)
///      -> edgeMesh(BRepAdaptor_Curve + GCPnts_TangentialDeflection)。
Result<TessellatedGeometry> tessellateTopoShape(const TopoDS_Shape& shape, const TessellationOptions& opts);

}  // namespace mulan::modeling::detail
