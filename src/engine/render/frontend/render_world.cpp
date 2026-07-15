#include "render_world.h"

#include <utility>

namespace mulan::engine {
namespace {

template <typename Handle>
uint64_t handleKey(Handle handle) {
    return (static_cast<uint64_t>(handle.generation) << 32u) | handle.index;
}

template <typename Record, typename Handle>
bool eraseRecord(std::vector<Record>& records, std::unordered_map<uint64_t, size_t>& positions, Handle handle) {
    const auto position = positions.find(handleKey(handle));
    if (position == positions.end()) {
        return false;
    }
    const size_t index = position->second;
    const size_t last = records.size() - 1;
    if (index != last) {
        records[index] = std::move(records[last]);
        positions[handleKey(records[index].handle)] = index;
    }
    records.pop_back();
    positions.erase(position);
    return true;
}

template <typename Record, typename Handle, typename Desc>
bool updateRecord(std::vector<Record>& records, const std::unordered_map<uint64_t, size_t>& positions, Handle handle,
                  Desc desc) {
    const auto position = positions.find(handleKey(handle));
    if (position == positions.end()) {
        return false;
    }
    records[position->second].desc = std::move(desc);
    return true;
}

std::shared_ptr<const RenderWorldStorage> emptyStorage() {
    static const auto empty = std::make_shared<const RenderWorldStorage>();
    return empty;
}

}  // namespace

RenderWorldSnapshot::RenderWorldSnapshot() : storage_(emptyStorage()) {
}

RenderWorldSnapshot::RenderWorldSnapshot(std::shared_ptr<const RenderWorldStorage> storage)
    : storage_(storage ? std::move(storage) : emptyStorage()) {
    for (const RenderObjectRecord& object : storage_->objects) {
        if (object.desc.visible) {
            bounds_.expand(object.desc.worldBounds);
        }
    }
}

const RenderGeometryRecord* RenderWorldSnapshot::geometry(GeometryHandle handle) const {
    const auto position = storage_->geometryPositions.find(handleKey(handle));
    return position == storage_->geometryPositions.end() ? nullptr : &storage_->geometries[position->second];
}

const RenderMaterialRecord* RenderWorldSnapshot::material(RenderMaterialHandle handle) const {
    const auto position = storage_->materialPositions.find(handleKey(handle));
    return position == storage_->materialPositions.end() ? nullptr : &storage_->materials[position->second];
}

RenderWorld::RenderWorld() : storage_(std::make_shared<RenderWorldStorage>()) {
}

void RenderWorld::ensureUniqueStorage() {
    if (storage_.use_count() != 1) {
        storage_ = std::make_shared<RenderWorldStorage>(*storage_);
    }
}

GeometryHandle RenderWorld::addGeometry(RenderGeometryDesc desc) {
    ensureUniqueStorage();
    const GeometryHandle handle{ .index = next_geometry_index_++, .generation = generation_ };
    storage_->geometryPositions.emplace(handleKey(handle), storage_->geometries.size());
    storage_->geometries.push_back(RenderGeometryRecord{ handle, std::move(desc) });
    return handle;
}

RenderMaterialHandle RenderWorld::addMaterial(RenderMaterialDesc desc) {
    ensureUniqueStorage();
    const RenderMaterialHandle handle{ .index = next_material_index_++, .generation = generation_ };
    storage_->materialPositions.emplace(handleKey(handle), storage_->materials.size());
    storage_->materials.push_back(RenderMaterialRecord{ handle, std::move(desc) });
    return handle;
}

RenderObjectId RenderWorld::addObject(RenderObjectDesc desc) {
    ensureUniqueStorage();
    const RenderObjectId id{ .index = next_object_index_++, .generation = generation_ };
    storage_->objectPositions.emplace(handleKey(id), storage_->objects.size());
    storage_->objects.push_back(RenderObjectRecord{ id, std::move(desc) });
    return id;
}

bool RenderWorld::updateGeometry(GeometryHandle handle, RenderGeometryDesc desc) {
    ensureUniqueStorage();
    return updateRecord(storage_->geometries, storage_->geometryPositions, handle, std::move(desc));
}

bool RenderWorld::updateMaterial(RenderMaterialHandle handle, RenderMaterialDesc desc) {
    ensureUniqueStorage();
    return updateRecord(storage_->materials, storage_->materialPositions, handle, std::move(desc));
}

bool RenderWorld::updateObject(RenderObjectId id, RenderObjectDesc desc) {
    ensureUniqueStorage();
    const auto position = storage_->objectPositions.find(handleKey(id));
    if (position == storage_->objectPositions.end()) {
        return false;
    }
    storage_->objects[position->second].desc = std::move(desc);
    return true;
}

bool RenderWorld::removeGeometry(GeometryHandle handle) {
    ensureUniqueStorage();
    return eraseRecord(storage_->geometries, storage_->geometryPositions, handle);
}

bool RenderWorld::removeMaterial(RenderMaterialHandle handle) {
    ensureUniqueStorage();
    return eraseRecord(storage_->materials, storage_->materialPositions, handle);
}

bool RenderWorld::removeObject(RenderObjectId id) {
    ensureUniqueStorage();
    const auto position = storage_->objectPositions.find(handleKey(id));
    if (position == storage_->objectPositions.end()) {
        return false;
    }
    const size_t index = position->second;
    const size_t last = storage_->objects.size() - 1;
    if (index != last) {
        storage_->objects[index] = std::move(storage_->objects[last]);
        storage_->objectPositions[handleKey(storage_->objects[index].id)] = index;
    }
    storage_->objects.pop_back();
    storage_->objectPositions.erase(position);
    return true;
}

void RenderWorld::clear() {
    storage_ = std::make_shared<RenderWorldStorage>();
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
