/**
 * @file shape_factory.h
 * @brief 从 OCCT TopoDS_Shape 构造中立 Shape 的入口。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * 仅供 modeling_occt 内部使用（签名带 TopoDS_Shape）。
 * io 层的 STEP 导入器通过 readStepFile 间接调用，不直接接触此头。
 */
#pragma once

#include "modeling_occt_export.h"

#include <mulan/modeling_core/shape.h>

#include <TopoDS_Shape.hxx>

namespace mulan::modeling {

/// 把 OCCT TopoDS_Shape 包成中立 Shape。
MODELING_OCCT_API Shape makeShape(const TopoDS_Shape& shape);

}  // namespace mulan::modeling
