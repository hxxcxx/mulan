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
#include <utility>

namespace mulan::engine {

/// 唯一标识 RenderWorld 的某个已发布状态；world 区分世界，revision 区分同一世界内的变更。
struct RenderWorldVersion {
    uint64_t world = 0;
    uint64_t revision = 0;

    friend constexpr bool operator==(RenderWorldVersion, RenderWorldVersion) = default;
};

/// RenderWorld 的持久化根集合；复制本对象只共享固定数量的不可变树根。
struct RenderWorldStorage {
    detail::PersistentRecordStore<RenderGeometryRecord> geometries;
    detail::PersistentRecordStore<RenderMaterialRecord> materials;
    detail::PersistentRecordStore<RenderObjectRecord> objects;
};

class RenderWorldSnapshot {
public:
    RenderWorldSnapshot();

    RenderWorldSnapshot(RenderWorldStorage storage, RenderWorldVersion version);

    const auto& geometries() const { return geometry_range_; }
    const auto& materials() const { return material_range_; }
    const auto& objects() const { return object_range_; }
    RenderWorldVersion version() const { return version_; }
    const math::AABB3& bounds() const;

    const RenderGeometryRecord* geometry(GeometryHandle handle) const;
    const RenderMaterialRecord* material(RenderMaterialHandle handle) const;
    const RenderObjectRecord* object(RenderObjectId id) const;

    template <typename Visitor>
    void forEachGeometryDifference(const RenderWorldSnapshot& previous, Visitor&& visitor) const {
        storage_.geometries.forEachDifference(previous.storage_.geometries, std::forward<Visitor>(visitor));
    }

    template <typename Visitor>
    void forEachMaterialDifference(const RenderWorldSnapshot& previous, Visitor&& visitor) const {
        storage_.materials.forEachDifference(previous.storage_.materials, std::forward<Visitor>(visitor));
    }

    template <typename Visitor>
    void forEachObjectDifference(const RenderWorldSnapshot& previous, Visitor&& visitor) const {
        storage_.objects.forEachDifference(previous.storage_.objects, std::forward<Visitor>(visitor));
    }

private:
    struct BoundsCache {
        std::once_flag once;
        math::AABB3 value;
    };

    RenderWorldStorage storage_;
    RenderWorldVersion version_;
    detail::PersistentRecordStore<RenderGeometryRecord>::Range geometry_range_;
    detail::PersistentRecordStore<RenderMaterialRecord>::Range material_range_;
    detail::PersistentRecordStore<RenderObjectRecord>::Range object_range_;
    std::shared_ptr<BoundsCache> bounds_cache_;
};

}  // namespace mulan::engine
