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
    RenderWorld();

    GeometryHandle addGeometry(RenderGeometryDesc desc);
    RenderMaterialHandle addMaterial(RenderMaterialDesc desc);
    RenderObjectId addObject(RenderObjectDesc desc);
    bool updateGeometry(GeometryHandle handle, RenderGeometryDesc desc);
    bool updateMaterial(RenderMaterialHandle handle, RenderMaterialDesc desc);
    bool updateObject(RenderObjectId id, RenderObjectDesc desc);
    bool removeGeometry(GeometryHandle handle);
    bool removeMaterial(RenderMaterialHandle handle);
    bool removeObject(RenderObjectId id);

    void clear();
    RenderWorldSnapshot snapshot() const;

    size_t geometryCount() const { return storage_->geometries.size(); }
    size_t materialCount() const { return storage_->materials.size(); }
    size_t objectCount() const { return storage_->objects.size(); }

private:
    void ensureUniqueStorage();

    std::shared_ptr<RenderWorldStorage> storage_;
    uint32_t next_geometry_index_ = 0;
    uint32_t next_material_index_ = 0;
    uint32_t next_object_index_ = 0;
    uint32_t generation_ = 1;
};

}  // namespace mulan::engine
