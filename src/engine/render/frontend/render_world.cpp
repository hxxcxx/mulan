#include "render_world.h"

#include <mutex>
#include <utility>

namespace mulan::engine {
namespace {

template <typename Handle>
bool sameHandle(Handle lhs, Handle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

}  // namespace

RenderWorldSnapshot::RenderWorldSnapshot()
    : geometry_range_(storage_.geometries.records()),
      material_range_(storage_.materials.records()),
      object_range_(storage_.objects.records()),
      bounds_cache_(std::make_shared<BoundsCache>()) {
}

RenderWorldSnapshot::RenderWorldSnapshot(RenderWorldStorage storage)
    : storage_(std::move(storage)),
      geometry_range_(storage_.geometries.records()),
      material_range_(storage_.materials.records()),
      object_range_(storage_.objects.records()),
      bounds_cache_(std::make_shared<BoundsCache>()) {
}

const math::AABB3& RenderWorldSnapshot::bounds() const {
    std::call_once(bounds_cache_->once, [&]() {
        for (const RenderObjectRecord& object : storage_.objects.records()) {
            if (object.desc.visible) {
                bounds_cache_->value.expand(object.desc.worldBounds);
            }
        }
    });
    return bounds_cache_->value;
}

const RenderGeometryRecord* RenderWorldSnapshot::geometry(GeometryHandle handle) const {
    const RenderGeometryRecord* record = storage_.geometries.find(handle.index);
    return record && sameHandle(record->handle, handle) ? record : nullptr;
}

const RenderMaterialRecord* RenderWorldSnapshot::material(RenderMaterialHandle handle) const {
    const RenderMaterialRecord* record = storage_.materials.find(handle.index);
    return record && sameHandle(record->handle, handle) ? record : nullptr;
}

RenderWorld::RenderWorld() = default;

GeometryHandle RenderWorld::addGeometry(RenderGeometryDesc desc) {
    const GeometryHandle handle{ .index = next_geometry_index_++, .generation = generation_ };
    storage_.geometries.set(handle.index, RenderGeometryRecord{ handle, std::move(desc) });
    return handle;
}

RenderMaterialHandle RenderWorld::addMaterial(RenderMaterialDesc desc) {
    const RenderMaterialHandle handle{ .index = next_material_index_++, .generation = generation_ };
    storage_.materials.set(handle.index, RenderMaterialRecord{ handle, std::move(desc) });
    return handle;
}

RenderObjectId RenderWorld::addObject(RenderObjectDesc desc) {
    const RenderObjectId id{ .index = next_object_index_++, .generation = generation_ };
    storage_.objects.set(id.index, RenderObjectRecord{ id, std::move(desc) });
    return id;
}

bool RenderWorld::updateGeometry(GeometryHandle handle, RenderGeometryDesc desc) {
    const RenderGeometryRecord* record = storage_.geometries.find(handle.index);
    if (!record || !sameHandle(record->handle, handle)) {
        return false;
    }
    storage_.geometries.set(handle.index, RenderGeometryRecord{ handle, std::move(desc) });
    return true;
}

bool RenderWorld::updateMaterial(RenderMaterialHandle handle, RenderMaterialDesc desc) {
    const RenderMaterialRecord* record = storage_.materials.find(handle.index);
    if (!record || !sameHandle(record->handle, handle)) {
        return false;
    }
    storage_.materials.set(handle.index, RenderMaterialRecord{ handle, std::move(desc) });
    return true;
}

bool RenderWorld::updateObject(RenderObjectId id, RenderObjectDesc desc) {
    const RenderObjectRecord* record = storage_.objects.find(id.index);
    if (!record || !sameHandle(record->id, id)) {
        return false;
    }
    storage_.objects.set(id.index, RenderObjectRecord{ id, std::move(desc) });
    return true;
}

bool RenderWorld::removeGeometry(GeometryHandle handle) {
    const RenderGeometryRecord* record = storage_.geometries.find(handle.index);
    return record && sameHandle(record->handle, handle) && storage_.geometries.erase(handle.index);
}

bool RenderWorld::removeMaterial(RenderMaterialHandle handle) {
    const RenderMaterialRecord* record = storage_.materials.find(handle.index);
    return record && sameHandle(record->handle, handle) && storage_.materials.erase(handle.index);
}

bool RenderWorld::removeObject(RenderObjectId id) {
    const RenderObjectRecord* record = storage_.objects.find(id.index);
    return record && sameHandle(record->id, id) && storage_.objects.erase(id.index);
}

void RenderWorld::clear() {
    storage_ = {};
    next_geometry_index_ = 0;
    next_material_index_ = 0;
    next_object_index_ = 0;
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
    }
}

RenderWorldSnapshot RenderWorld::snapshot() const {
    return RenderWorldSnapshot{ storage_ };
}

}  // namespace mulan::engine
