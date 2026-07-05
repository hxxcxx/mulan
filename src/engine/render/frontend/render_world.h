/**
 * @file render_world.h
 * @brief RenderWorld 保存 engine frontend 的 CPU 渲染对象、几何和材质描述。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_world_snapshot.h"

namespace mulan::engine {

class RenderWorld {
public:
    RenderWorld() = default;

    GeometryHandle addGeometry(RenderGeometryDesc desc);
    RenderMaterialHandle addMaterial(RenderMaterialDesc desc);
    RenderObjectId addObject(RenderObjectDesc desc);

    void clear();
    RenderWorldSnapshot snapshot() const;

    size_t geometryCount() const { return geometries_.size(); }
    size_t materialCount() const { return materials_.size(); }
    size_t objectCount() const { return objects_.size(); }

private:
    std::vector<RenderGeometryRecord> geometries_;
    std::vector<RenderMaterialRecord> materials_;
    std::vector<RenderObjectRecord> objects_;
    math::AABB3 bounds_;
    uint32_t generation_ = 1;
};

} // namespace mulan::engine
