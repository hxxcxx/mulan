/**
 * @file render_world_snapshot.h
 * @brief RenderWorldSnapshot 是 RenderWorld 的不可变 CPU 渲染快照。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_object.h"
#include "render_record_store.h"

#include <memory>
#include <mutex>

namespace mulan::engine {

/// 唯一标识 RenderWorld 的某个已发布状态；world 区分世界，revision 区分世界内的变更。
struct RenderWorldVersion {
    uint64_t world = 0;
    uint64_t revision = 0;

    friend constexpr bool operator==(RenderWorldVersion, RenderWorldVersion) = default;
};

/// RenderWorld 的记录集合；由 RenderWorld 在首个写操作时整体复制。
struct RenderWorldStorage {
    detail::RenderRecordStore<RenderGeometryRecord> geometries;
    detail::RenderRecordStore<RenderMaterialRecord> materials;
    detail::RenderRecordStore<RenderObjectRecord> objects;
};

class RenderWorldSnapshot {
public:
    RenderWorldSnapshot();
    RenderWorldSnapshot(std::shared_ptr<const RenderWorldStorage> storage, RenderWorldVersion version);

    auto geometries() const { return storage_->geometries.records(); }
    auto materials() const { return storage_->materials.records(); }
    auto objects() const { return storage_->objects.records(); }
    RenderWorldVersion version() const { return version_; }
    const math::AABB3& bounds() const;

    const RenderGeometryRecord* geometry(GeometryHandle handle) const;
    const RenderMaterialRecord* material(RenderMaterialHandle handle) const;
    const RenderObjectRecord* object(RenderObjectId id) const;

private:
    struct BoundsCache {
        std::once_flag once;
        math::AABB3 value;
    };

    std::shared_ptr<const RenderWorldStorage> storage_;
    RenderWorldVersion version_;
    std::shared_ptr<BoundsCache> bounds_cache_;
};

}  // namespace mulan::engine
