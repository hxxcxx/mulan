/**
 * @file truck_shape_ops.h
 * @brief truck-bridge 后端的 IShapeOps 实现。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * TruckShapeOps 经 backend_entry.cpp 以 truck 名称注册到 ShapeOpsRegistry。
 * 只有启用 MULAN_ENABLE_TRUCK_BACKEND 并通过 MULAN_SHAPE_OPS_BACKEND 选择 truck
 * 时才接管建模操作；文件读写仍由 OCCT 提供。
 */
#pragma once

#include <mulan/modeling/core/shape_ops.h>

namespace mulan::modeling {

class TruckShapeOps final : public IShapeOps {
public:
    core::Result<Shape> extrude(const ExtrudeParams& params) override;
    core::Result<Shape> boolean(const Shape& target, const Shape& tool, BooleanOp op) override;
};

}  // namespace mulan::modeling
