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
    RenderWorld(const RenderWorld&) = delete;
    RenderWorld& operator=(const RenderWorld&) = delete;

    GeometryHandle addGeometry(RenderGeometryDesc desc);
    RenderMaterialHandle addMaterial(RenderMaterialDesc desc);
    RenderObjectId addObject(RenderObjectDesc desc);
    /// update* 对有效句柄是幂等操作；描述未变化时返回 true，但不复制存储、不推进 revision。
    bool updateGeometry(GeometryHandle handle, RenderGeometryDesc desc);
    bool updateMaterial(RenderMaterialHandle handle, RenderMaterialDesc desc);
    bool updateObject(RenderObjectId id, RenderObjectDesc desc);
    /// 仅更新对象的空间状态，保留 PickId、drawable 及资源句柄。
    /// 句柄失效时返回 false；内容未变化时成功但不推进 world revision。
    bool updateObjectSpatialState(RenderObjectId id, const math::Mat4& worldTransform, const math::AABB3& worldBounds,
                                  bool visible);
    bool removeGeometry(GeometryHandle handle);
    bool removeMaterial(RenderMaterialHandle handle);
    bool removeObject(RenderObjectId id);

    void clear();
    RenderWorldSnapshot snapshot() const;

    size_t geometryCount() const { return storage_->geometries.size(); }
    size_t materialCount() const { return storage_->materials.size(); }
    size_t objectCount() const { return storage_->objects.size(); }

private:
    void ensureWritable();
    void advanceRevision();

    std::shared_ptr<RenderWorldStorage> storage_;
    RenderWorldVersion version_;
    uint32_t next_geometry_index_ = 0;
    uint32_t next_material_index_ = 0;
    uint32_t next_object_index_ = 0;
    uint32_t generation_ = 1;
};

}  // namespace mulan::engine
