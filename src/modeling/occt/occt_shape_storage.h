/**
 * @file occt_shape_storage.h
 * @brief ShapeStorage 的 OCCT 具体实现（OCCT 唯一合法驻地之一）。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * 本头可被 modeling_occt 内部其它 .cpp 包含；它带 TopoDS_Shape，因此**禁止**
 * 被 modeling_core / io / asset / scene / view / app 包含。
 */
#pragma once

#include "modeling_occt_export.h"

#include <mulan/modeling/core/shape.h>

#include <TopoDS_Shape.hxx>

namespace mulan::modeling {

/// OCCT 后端的 B-Rep 拓扑存储：内部持有 TopoDS_Shape。
///
/// tessellate() 转发到 occt_tessellator_internal 的离散算法，保持 storage 实现与
/// 离散算法分离。
class OcctShapeStorage final : public ShapeStorage {
public:
    explicit OcctShapeStorage(TopoDS_Shape shape);

    const TopoDS_Shape& topoShape() const { return shape_; }

    BodyKind bodyKind() const override;
    math::AABB3 bounds() const override;
    core::Result<TessellatedGeometry> tessellate(const TessellationOptions& opts) const override;

private:
    TopoDS_Shape shape_;
};

}  // namespace mulan::modeling
