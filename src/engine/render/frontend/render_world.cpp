#include "render_world.h"

#include <atomic>
#include <exception>
#include <limits>
#include <utility>

namespace mulan::engine {
namespace {

template <typename Handle>
bool sameHandle(Handle lhs, Handle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

bool sameMatrix(const math::Mat4& lhs, const math::Mat4& rhs) {
    for (int column = 0; column < 4; ++column) {
        if (lhs[column] != rhs[column])
            return false;
    }
    return true;
}

bool sameBounds(const math::AABB3& lhs, const math::AABB3& rhs) {
    if (lhs.isEmpty() || rhs.isEmpty())
        return lhs.isEmpty() == rhs.isEmpty();
    return lhs.min == rhs.min && lhs.max == rhs.max;
}

std::atomic<uint64_t> next_render_world_id{ 1 };

uint64_t allocateRenderWorldId() {
    const uint64_t id = next_render_world_id.fetch_add(1, std::memory_order_relaxed);
    // 0 保留给无归属快照；身份空间耗尽后无法继续保证缓存隔离。
    if (id == 0)
        std::terminate();
    return id;
}

}  // namespace

RenderWorldSnapshot::RenderWorldSnapshot()
    : storage_(std::make_shared<RenderWorldStorage>()), bounds_cache_(std::make_shared<BoundsCache>()) {
}

RenderWorldSnapshot::RenderWorldSnapshot(std::shared_ptr<const RenderWorldStorage> storage, RenderWorldVersion version)
    : storage_(std::move(storage)), version_(version), bounds_cache_(std::make_shared<BoundsCache>()) {
}

const math::AABB3& RenderWorldSnapshot::bounds() const {
    std::call_once(bounds_cache_->once, [&]() {
        for (const RenderObjectRecord& object : storage_->objects.records()) {
            if (object.desc.visible)
                bounds_cache_->value.expand(object.desc.worldBounds);
        }
    });
    return bounds_cache_->value;
}

const RenderGeometryRecord* RenderWorldSnapshot::geometry(GeometryHandle handle) const {
    const RenderGeometryRecord* record = storage_->geometries.find(handle.index);
    return record && sameHandle(record->handle, handle) ? record : nullptr;
}

const RenderMaterialRecord* RenderWorldSnapshot::material(RenderMaterialHandle handle) const {
    const RenderMaterialRecord* record = storage_->materials.find(handle.index);
    return record && sameHandle(record->handle, handle) ? record : nullptr;
}

const RenderObjectRecord* RenderWorldSnapshot::object(RenderObjectId id) const {
    const RenderObjectRecord* record = storage_->objects.find(id.index);
    return record && sameHandle(record->id, id) ? record : nullptr;
}

RenderWorld::RenderWorld()
    : storage_(std::make_shared<RenderWorldStorage>()), version_{ .world = allocateRenderWorldId() } {
}

void RenderWorld::ensureWritable() {
    if (storage_.use_count() != 1)
        storage_ = std::make_shared<RenderWorldStorage>(*storage_);
}

void RenderWorld::advanceRevision() {
    if (version_.revision == std::numeric_limits<uint64_t>::max()) {
        version_.world = allocateRenderWorldId();
        version_.revision = 1;
        return;
    }
    ++version_.revision;
}

GeometryHandle RenderWorld::addGeometry(RenderGeometryDesc desc) {
    ensureWritable();
    const GeometryHandle handle{ .index = next_geometry_index_++, .generation = generation_ };
    storage_->geometries.set(handle.index, RenderGeometryRecord{ handle, std::move(desc) });
    advanceRevision();
    return handle;
}

RenderMaterialHandle RenderWorld::addMaterial(RenderMaterialDesc desc) {
    ensureWritable();
    const RenderMaterialHandle handle{ .index = next_material_index_++, .generation = generation_ };
    storage_->materials.set(handle.index, RenderMaterialRecord{ handle, std::move(desc) });
    advanceRevision();
    return handle;
}

RenderObjectId RenderWorld::addObject(RenderObjectDesc desc) {
    ensureWritable();
    const RenderObjectId id{ .index = next_object_index_++, .generation = generation_ };
    storage_->objects.set(id.index, RenderObjectRecord{ id, std::move(desc) });
    advanceRevision();
    return id;
}

bool RenderWorld::updateGeometry(GeometryHandle handle, RenderGeometryDesc desc) {
    const RenderGeometryRecord* record = storage_->geometries.find(handle.index);
    if (!record || !sameHandle(record->handle, handle))
        return false;
    ensureWritable();
    storage_->geometries.set(handle.index, RenderGeometryRecord{ handle, std::move(desc) });
    advanceRevision();
    return true;
}

bool RenderWorld::updateMaterial(RenderMaterialHandle handle, RenderMaterialDesc desc) {
    const RenderMaterialRecord* record = storage_->materials.find(handle.index);
    if (!record || !sameHandle(record->handle, handle))
        return false;
    ensureWritable();
    storage_->materials.set(handle.index, RenderMaterialRecord{ handle, std::move(desc) });
    advanceRevision();
    return true;
}

bool RenderWorld::updateObject(RenderObjectId id, RenderObjectDesc desc) {
    const RenderObjectRecord* record = storage_->objects.find(id.index);
    if (!record || !sameHandle(record->id, id))
        return false;
    ensureWritable();
    storage_->objects.set(id.index, RenderObjectRecord{ id, std::move(desc) });
    advanceRevision();
    return true;
}

bool RenderWorld::updateObjectSpatialState(RenderObjectId id, const math::Mat4& worldTransform,
                                           const math::AABB3& worldBounds, bool visible) {
    const RenderObjectRecord* record = storage_->objects.find(id.index);
    if (!record || !sameHandle(record->id, id))
        return false;
    if (sameMatrix(record->desc.worldTransform, worldTransform) && sameBounds(record->desc.worldBounds, worldBounds) &&
        record->desc.visible == visible) {
        return true;
    }

    RenderObjectDesc desc = record->desc;
    desc.worldTransform = worldTransform;
    desc.worldBounds = worldBounds;
    desc.visible = visible;
    ensureWritable();
    storage_->objects.set(id.index, RenderObjectRecord{ id, std::move(desc) });
    advanceRevision();
    return true;
}

bool RenderWorld::removeGeometry(GeometryHandle handle) {
    const RenderGeometryRecord* record = storage_->geometries.find(handle.index);
    if (!record || !sameHandle(record->handle, handle))
        return false;
    ensureWritable();
    storage_->geometries.erase(handle.index);
    advanceRevision();
    return true;
}

bool RenderWorld::removeMaterial(RenderMaterialHandle handle) {
    const RenderMaterialRecord* record = storage_->materials.find(handle.index);
    if (!record || !sameHandle(record->handle, handle))
        return false;
    ensureWritable();
    storage_->materials.erase(handle.index);
    advanceRevision();
    return true;
}

bool RenderWorld::removeObject(RenderObjectId id) {
    const RenderObjectRecord* record = storage_->objects.find(id.index);
    if (!record || !sameHandle(record->id, id))
        return false;
    ensureWritable();
    storage_->objects.erase(id.index);
    advanceRevision();
    return true;
}

void RenderWorld::clear() {
    storage_ = std::make_shared<RenderWorldStorage>();
    next_geometry_index_ = 0;
    next_material_index_ = 0;
    next_object_index_ = 0;
    if (++generation_ == 0)
        generation_ = 1;
    advanceRevision();
}

RenderWorldSnapshot RenderWorld::snapshot() const {
    return RenderWorldSnapshot{ storage_, version_ };
}

}  // namespace mulan::engine
