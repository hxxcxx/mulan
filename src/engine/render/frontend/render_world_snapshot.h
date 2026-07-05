/**
 * @file render_world_snapshot.h
 * @brief RenderWorldSnapshot 是 RenderWorld 的不可变 CPU 渲染快照。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_object.h"

#include <span>
#include <vector>

namespace mulan::engine {

class RenderWorldSnapshot {
public:
    RenderWorldSnapshot() = default;

    RenderWorldSnapshot(std::vector<RenderGeometryRecord> geometries, std::vector<RenderMaterialRecord> materials,
                        std::vector<RenderObjectRecord> objects, math::AABB3 bounds);

    std::span<const RenderGeometryRecord> geometries() const { return geometries_; }
    std::span<const RenderMaterialRecord> materials() const { return materials_; }
    std::span<const RenderObjectRecord> objects() const { return objects_; }
    const math::AABB3& bounds() const { return bounds_; }

    const RenderGeometryRecord* geometry(GeometryHandle handle) const;
    const RenderMaterialRecord* material(RenderMaterialHandle handle) const;

private:
    std::vector<RenderGeometryRecord> geometries_;
    std::vector<RenderMaterialRecord> materials_;
    std::vector<RenderObjectRecord> objects_;
    math::AABB3 bounds_;
};

}  // namespace mulan::engine
