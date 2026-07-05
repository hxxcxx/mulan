#include "render_world.h"

#include <utility>

namespace mulan::engine {

RenderWorldSnapshot::RenderWorldSnapshot(std::vector<RenderGeometryRecord> geometries,
                                         std::vector<RenderMaterialRecord> materials,
                                         std::vector<RenderObjectRecord> objects,
                                         math::AABB3 bounds)
    : geometries_(std::move(geometries)),
      materials_(std::move(materials)),
      objects_(std::move(objects)),
      bounds_(bounds) {
}

const RenderGeometryRecord* RenderWorldSnapshot::geometry(GeometryHandle handle) const {
    if (!handle || handle.index >= geometries_.size()) return nullptr;
    const auto& record = geometries_[handle.index];
    return record.handle == handle ? &record : nullptr;
}

const RenderMaterialRecord* RenderWorldSnapshot::material(RenderMaterialHandle handle) const {
    if (!handle || handle.index >= materials_.size()) return nullptr;
    const auto& record = materials_[handle.index];
    return record.handle == handle ? &record : nullptr;
}

GeometryHandle RenderWorld::addGeometry(RenderGeometryDesc desc) {
    GeometryHandle handle{
        .index = static_cast<uint32_t>(geometries_.size()),
        .generation = generation_,
    };
    geometries_.push_back(RenderGeometryRecord{handle, std::move(desc)});
    return handle;
}

RenderMaterialHandle RenderWorld::addMaterial(RenderMaterialDesc desc) {
    RenderMaterialHandle handle{
        .index = static_cast<uint32_t>(materials_.size()),
        .generation = generation_,
    };
    materials_.push_back(RenderMaterialRecord{handle, std::move(desc)});
    return handle;
}

RenderObjectId RenderWorld::addObject(RenderObjectDesc desc) {
    RenderObjectId id{
        .index = static_cast<uint32_t>(objects_.size()),
        .generation = generation_,
    };
    if (desc.visible) {
        bounds_.expand(desc.worldBounds);
    }
    objects_.push_back(RenderObjectRecord{id, std::move(desc)});
    return id;
}

void RenderWorld::clear() {
    geometries_.clear();
    materials_.clear();
    objects_.clear();
    bounds_.reset();
    ++generation_;
    if (generation_ == 0) generation_ = 1;
}

RenderWorldSnapshot RenderWorld::snapshot() const {
    return RenderWorldSnapshot{geometries_, materials_, objects_, bounds_};
}

} // namespace mulan::engine
