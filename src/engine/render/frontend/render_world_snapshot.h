/**
 * @file render_world_snapshot.h
 * @brief RenderWorldSnapshot 是 RenderWorld 的不可变 CPU 渲染快照。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_object.h"

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace mulan::engine {

/// RenderWorld 与其不可变快照共享的版本化存储。写入方在修改前执行 copy-on-write。
struct RenderWorldStorage {
    std::vector<RenderGeometryRecord> geometries;
    std::vector<RenderMaterialRecord> materials;
    std::vector<RenderObjectRecord> objects;
    std::unordered_map<uint64_t, size_t> geometryPositions;
    std::unordered_map<uint64_t, size_t> materialPositions;
    std::unordered_map<uint64_t, size_t> objectPositions;
};

class RenderWorldSnapshot {
public:
    RenderWorldSnapshot();

    explicit RenderWorldSnapshot(std::shared_ptr<const RenderWorldStorage> storage);

    std::span<const RenderGeometryRecord> geometries() const { return storage_->geometries; }
    std::span<const RenderMaterialRecord> materials() const { return storage_->materials; }
    std::span<const RenderObjectRecord> objects() const { return storage_->objects; }
    const math::AABB3& bounds() const { return bounds_; }

    const RenderGeometryRecord* geometry(GeometryHandle handle) const;
    const RenderMaterialRecord* material(RenderMaterialHandle handle) const;

private:
    std::shared_ptr<const RenderWorldStorage> storage_;
    math::AABB3 bounds_;
};

}  // namespace mulan::engine
