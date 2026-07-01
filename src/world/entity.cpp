#include "entity.h"
#include "world.h"

namespace mulan::world {

Entity::Entity(Id id, std::string name)
    : id_(id)
    , name_(std::move(name))
    , local_transform_(1.0)
    , world_transform_(1.0) {
    // 注意：不在此处 markDirty(Created) —— 此时 world_ 尚未设置，
    // 标记无法传给 World。Created 由 World::createEntity 在 setWorldPtr 之后补标。
}

// --- setter 自动标脏 ---

void Entity::markDirty(EntityDirty d) {
    dirty_flags_ |= static_cast<uint64_t>(d);
    if (world_) world_->markDirty(id_, d);
}

void Entity::setName(std::string name) {
    if (name_ != name) {
        name_ = std::move(name);
        markDirty(EntityDirty::Name);
    }
}

void Entity::setLocalTransform(const engine::Mat4& t) {
    local_transform_ = t;
    markDirty(EntityDirty::Transform);
}

void Entity::setVisible(bool v) {
    if (visible_ != v) {
        visible_ = v;
        markDirty(EntityDirty::Visibility);
    }
}

void Entity::setSelected(bool s) {
    if (selected_ != s) {
        selected_ = s;
        markDirty(EntityDirty::Selection);
    }
}

void Entity::setMaterialId(uint16_t id) {
    if (material_id_ != id) {
        material_id_ = id;
        markDirty(EntityDirty::Material);
    }
}

void Entity::setGeometry(std::unique_ptr<GeometryData> geo) {
    geometry_ = std::move(geo);
    cached_face_mesh_ = engine::Mesh{};
    cached_edge_mesh_ = engine::Mesh{};
    markDirty(EntityDirty::Geometry);
}

const engine::Mesh& Entity::cachedFaceMesh() const {
    if (cached_face_mesh_.empty() && geometry_) {
        cached_face_mesh_ = geometry_->faceMesh();
        cached_edge_mesh_ = geometry_->edgeMesh();
    }
    return cached_face_mesh_;
}

const engine::Mesh& Entity::cachedEdgeMesh() const {
    if (cached_edge_mesh_.empty() && geometry_) {
        cached_face_mesh_ = geometry_->faceMesh();
        cached_edge_mesh_ = geometry_->edgeMesh();
    }
    return cached_edge_mesh_;
}

bool Entity::valid(const World& world) const {
    return id_ != INVALID_ID && world.isValid(id_);
}

} // namespace mulan::world
