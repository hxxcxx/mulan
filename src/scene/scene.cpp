#include "scene.h"

#include <utility>

namespace mulan::scene {

EntityId Scene::allocateId() {
    uint32_t index = 0;
    if (!free_indices_.empty()) {
        index = free_indices_.back().index();
        free_indices_.pop_back();
    } else {
        index = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }

    Slot& slot = slots_[index];
    slot.alive = true;
    return EntityId{(static_cast<uint64_t>(slot.generation) << EntityId::INDEX_BITS) | index};
}

EntityId Scene::createEntity(std::string name) {
    EntityId id = allocateId();
    entities_.push_back(id);

    names_.emplace(id, NameComponent{std::move(name)});
    transforms_.emplace(id, TransformComponent{});
    hierarchies_.emplace(id, HierarchyComponent{});
    geometries_.emplace(id, GeometryComponent{});
    renders_.emplace(id, RenderComponent{});
    selections_.emplace(id, SelectionComponent{});
    bounds_.emplace(id, BoundsComponent{});

    markDirty(id, EntityDirty::Created);
    return id;
}

void Scene::destroyEntity(EntityId id) {
    if (!isValid(id)) return;

    auto childIt = children_.find(id);
    if (childIt != children_.end()) {
        auto children = std::move(childIt->second);
        children_.erase(childIt);
        for (auto childId : children) {
            if (auto* h = hierarchy(childId)) {
                h->parent = EntityId::invalid();
                markDirty(childId, EntityDirty::Hierarchy | EntityDirty::Transform);
            }
        }
    }

    if (auto* h = hierarchy(id); h && h->parent) {
        removeChild(h->parent, id);
    }

    markDirty(id, EntityDirty::Destroyed);
    eraseComponents(id);

    entities_.erase(std::remove(entities_.begin(), entities_.end(), id), entities_.end());

    Slot& slot = slots_[indexOf(id)];
    slot.alive = false;
    ++slot.generation;
    free_indices_.push_back(EntityId{static_cast<uint64_t>(indexOf(id))});
}

bool Scene::isValid(EntityId id) const {
    if (!id) return false;
    uint32_t index = indexOf(id);
    if (index >= slots_.size()) return false;
    const Slot& slot = slots_[index];
    return slot.alive && slot.generation == id.generation();
}

bool Scene::setParent(EntityId child, EntityId parent) {
    if (!isValid(child)) return false;

    auto* h = hierarchy(child);
    if (!h) return false;

    if (!parent) {
        if (h->parent)
            removeChild(h->parent, child);
        h->parent = EntityId::invalid();
        markDirty(child, EntityDirty::Hierarchy | EntityDirty::Transform);
        return true;
    }

    if (!isValid(parent) || detectCycle(child, parent))
        return false;

    if (h->parent)
        removeChild(h->parent, child);

    h->parent = parent;
    addChild(parent, child);
    markDirty(child, EntityDirty::Hierarchy | EntityDirty::Transform);
    return true;
}

const std::vector<EntityId>& Scene::childrenOf(EntityId parent) const {
    static const std::vector<EntityId> empty;
    auto it = children_.find(parent);
    return it != children_.end() ? it->second : empty;
}

NameComponent* Scene::name(EntityId id) {
    auto it = names_.find(id);
    return it != names_.end() ? &it->second : nullptr;
}

const NameComponent* Scene::name(EntityId id) const {
    auto it = names_.find(id);
    return it != names_.end() ? &it->second : nullptr;
}

TransformComponent* Scene::transform(EntityId id) {
    auto it = transforms_.find(id);
    return it != transforms_.end() ? &it->second : nullptr;
}

const TransformComponent* Scene::transform(EntityId id) const {
    auto it = transforms_.find(id);
    return it != transforms_.end() ? &it->second : nullptr;
}

HierarchyComponent* Scene::hierarchy(EntityId id) {
    auto it = hierarchies_.find(id);
    return it != hierarchies_.end() ? &it->second : nullptr;
}

const HierarchyComponent* Scene::hierarchy(EntityId id) const {
    auto it = hierarchies_.find(id);
    return it != hierarchies_.end() ? &it->second : nullptr;
}

GeometryComponent* Scene::geometry(EntityId id) {
    auto it = geometries_.find(id);
    return it != geometries_.end() ? &it->second : nullptr;
}

const GeometryComponent* Scene::geometry(EntityId id) const {
    auto it = geometries_.find(id);
    return it != geometries_.end() ? &it->second : nullptr;
}

RenderComponent* Scene::render(EntityId id) {
    auto it = renders_.find(id);
    return it != renders_.end() ? &it->second : nullptr;
}

const RenderComponent* Scene::render(EntityId id) const {
    auto it = renders_.find(id);
    return it != renders_.end() ? &it->second : nullptr;
}

SelectionComponent* Scene::selection(EntityId id) {
    auto it = selections_.find(id);
    return it != selections_.end() ? &it->second : nullptr;
}

const SelectionComponent* Scene::selection(EntityId id) const {
    auto it = selections_.find(id);
    return it != selections_.end() ? &it->second : nullptr;
}

BoundsComponent* Scene::bounds(EntityId id) {
    auto it = bounds_.find(id);
    return it != bounds_.end() ? &it->second : nullptr;
}

const BoundsComponent* Scene::bounds(EntityId id) const {
    auto it = bounds_.find(id);
    return it != bounds_.end() ? &it->second : nullptr;
}

void Scene::markDirty(EntityId id, EntityDirty dirty) {
    dirty_[id] |= dirtyValue(dirty);
}

uint64_t Scene::dirtyFlags(EntityId id) const {
    auto it = dirty_.find(id);
    return it != dirty_.end() ? it->second : 0;
}

void Scene::clearDirty(EntityDirty mask) {
    uint64_t keep = ~dirtyValue(mask);
    for (auto it = dirty_.begin(); it != dirty_.end();) {
        it->second &= keep;
        if (it->second == 0)
            it = dirty_.erase(it);
        else
            ++it;
    }
}

bool Scene::detectCycle(EntityId child, EntityId parent) const {
    if (child == parent) return true;

    EntityId cursor = parent;
    while (cursor) {
        if (cursor == child) return true;
        auto* h = hierarchy(cursor);
        if (!h) break;
        cursor = h->parent;
    }
    return false;
}

void Scene::addChild(EntityId parent, EntityId child) {
    children_[parent].push_back(child);
}

void Scene::removeChild(EntityId parent, EntityId child) {
    auto it = children_.find(parent);
    if (it == children_.end()) return;

    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
    if (vec.empty())
        children_.erase(it);
}

void Scene::eraseComponents(EntityId id) {
    names_.erase(id);
    transforms_.erase(id);
    hierarchies_.erase(id);
    geometries_.erase(id);
    renders_.erase(id);
    selections_.erase(id);
    bounds_.erase(id);
}

} // namespace mulan::scene

