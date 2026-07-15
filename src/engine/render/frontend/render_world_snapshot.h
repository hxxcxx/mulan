/**
 * @file render_world_snapshot.h
 * @brief RenderWorldSnapshot 是 RenderWorld 的不可变 CPU 渲染快照。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "persistent_record_store.h"
#include "render_object.h"

#include <memory>
#include <mutex>

namespace mulan::engine {

/// RenderWorld 的持久化根集合；复制本对象只共享固定数量的不可变树根。
struct RenderWorldStorage {
    detail::PersistentRecordStore<RenderGeometryRecord> geometries;
    detail::PersistentRecordStore<RenderMaterialRecord> materials;
    detail::PersistentRecordStore<RenderObjectRecord> objects;
};

class RenderWorldSnapshot {
public:
    RenderWorldSnapshot();

    explicit RenderWorldSnapshot(RenderWorldStorage storage);

    const auto& geometries() const { return geometry_range_; }
    const auto& materials() const { return material_range_; }
    const auto& objects() const { return object_range_; }
    const math::AABB3& bounds() const;

    const RenderGeometryRecord* geometry(GeometryHandle handle) const;
    const RenderMaterialRecord* material(RenderMaterialHandle handle) const;

private:
    struct BoundsCache {
        std::once_flag once;
        math::AABB3 value;
    };

    RenderWorldStorage storage_;
    detail::PersistentRecordStore<RenderGeometryRecord>::Range geometry_range_;
    detail::PersistentRecordStore<RenderMaterialRecord>::Range material_range_;
    detail::PersistentRecordStore<RenderObjectRecord>::Range object_range_;
    std::shared_ptr<BoundsCache> bounds_cache_;
};

}  // namespace mulan::engine
