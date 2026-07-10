/**
 * @file truck_shape_storage.h
 * @brief ShapeStorage 的 truck-bridge 具体实现：内部持有 TruckSolid 句柄。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 本头只允许 modeling_truck 内部使用；公共层仍通过中立 Shape/ShapeStorage
 * 访问 B-Rep，不暴露 truck-bridge 的 C ABI 类型。
 */
#pragma once

#include <mulan/modeling/core/shape.h>

struct TruckSolid;

namespace mulan::modeling {

class TruckShapeStorage final : public ShapeStorage {
public:
    explicit TruckShapeStorage(TruckSolid* solid);
    ~TruckShapeStorage() override;

    TruckShapeStorage(const TruckShapeStorage&) = delete;
    TruckShapeStorage& operator=(const TruckShapeStorage&) = delete;

    const TruckSolid* solid() const { return solid_; }

    BodyKind bodyKind() const override;
    math::AABB3 bounds() const override;
    core::Result<TessellatedGeometry> tessellate(const TessellationOptions& opts) const override;

private:
    TruckSolid* solid_ = nullptr;
};

Shape makeTruckShape(TruckSolid* solid);

}  // namespace mulan::modeling
