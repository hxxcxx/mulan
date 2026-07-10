/**
 * @file occt_shape_ops.h
 * @brief OCCT 后端的建模操作实现(拉伸/布尔)。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 模块内部头:经 backend_entry 的 mulan_load_backend 注册到 ShapeOpsRegistry。
 * OCCT 头只在 .cpp 出现。
 */
#pragma once

#include <mulan/modeling/core/shape_ops.h>

namespace mulan::modeling {

/// OCCT 后端建模操作:拉伸(BRepPrimAPI_MakePrism)+ 布尔(BRepAlgoAPI_Cut/Fuse/Common)。
class OccShapeOps final : public IShapeOps {
public:
    core::Result<Shape> extrude(const ExtrudeParams& params) override;
    core::Result<Shape> boolean(const Shape& target, const Shape& tool, BooleanOp op) override;
};

}  // namespace mulan::modeling
