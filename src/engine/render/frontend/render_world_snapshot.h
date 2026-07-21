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

namespace mulan::engine {

/// 唯一标识 RenderWorld 的某个已发布状态。revision 记录所有变化；packetRevision
/// 与 spatialRevision 分别驱动绘制 Packet 和空间可见性索引，避免无关缓存联动失效。
struct RenderWorldVersion {
    uint64_t world = 0;
    uint64_t revision = 0;
    uint64_t packetRevision = 0;
    uint64_t spatialRevision = 0;

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

    const RenderGeometryRecord* geometry(GeometryHandle handle) const;
    const RenderMaterialRecord* material(RenderMaterialHandle handle) const;
    const RenderObjectRecord* object(RenderObjectId id) const;

private:
    std::shared_ptr<const RenderWorldStorage> storage_;
    RenderWorldVersion version_;
};

}  // namespace mulan::engine
