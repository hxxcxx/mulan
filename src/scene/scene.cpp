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
    return EntityId{ (static_cast<uint64_t>(slot.generation) << EntityId::INDEX_BITS) | index };
}

EntityId Scene::createEntity(std::string name) {
    EntityId id = allocateId();
    entities_.push_back(id);

    names_.emplace(id, NameComponent{ std::move(name) });
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
    if (!isValid(id))
        return;

    auto childIt = children_.find(id);
    if (childIt != children_.end()) {
        auto children = std::move(childIt->second);
        children_.erase(childIt);
        for (auto childId : children) {
            if (auto* h = mutableHierarchy(childId)) {
                h->parent = EntityId::invalid();
                markDirty(childId, EntityDirty::Hierarchy | EntityDirty::Transform);
            }
        }
    }

    if (auto* h = mutableHierarchy(id); h && h->parent) {
        removeChild(h->parent, id);
    }

    markDirty(id, EntityDirty::Destroyed);
    eraseComponents(id);

    entities_.erase(std::remove(entities_.begin(), entities_.end(), id), entities_.end());

    Slot& slot = slots_[indexOf(id)];
    slot.alive = false;
    ++slot.generation;
    free_indices_.push_back(EntityId{ static_cast<uint64_t>(indexOf(id)) });
}

bool Scene::isValid(EntityId id) const {
    if (!id)
        return false;
    uint32_t index = indexOf(id);
    if (index >= slots_.size())
        return false;
    const Slot& slot = slots_[index];
    return slot.alive && slot.generation == id.generation();
}

bool Scene::setParent(EntityId child, EntityId parent) {
    if (!isValid(child))
        return false;

    auto* h = mutableHierarchy(child);
    if (!h)
        return false;

    if (!parent) {
        if (h->parent)
            removeChild(h->parent, child);
        h->parent = EntityId::invalid();
        // 脱离父级后 world = local（按根节点重算子树）
        updateWorldRecursive(child);
        return true;
    }

    if (!isValid(parent) || detectCycle(child, parent))
        return false;

    if (h->parent)
        removeChild(h->parent, child);

    h->parent = parent;
    addChild(parent, child);
    // parent 变化后，该子树的 world 矩阵需按新父级重算
    updateWorldRecursive(child);
    return true;
}

const std::vector<EntityId>& Scene::childrenOf(EntityId parent) const {
    static const std::vector<EntityId> empty;
    auto it = children_.find(parent);
    return it != children_.end() ? it->second : empty;
}

const NameComponent* Scene::name(EntityId id) const {
    auto it = names_.find(id);
    return it != names_.end() ? &it->second : nullptr;
}

const TransformComponent* Scene::transform(EntityId id) const {
    auto it = transforms_.find(id);
    return it != transforms_.end() ? &it->second : nullptr;
}

const HierarchyComponent* Scene::hierarchy(EntityId id) const {
    auto it = hierarchies_.find(id);
    return it != hierarchies_.end() ? &it->second : nullptr;
}

const GeometryComponent* Scene::geometry(EntityId id) const {
    auto it = geometries_.find(id);
    return it != geometries_.end() ? &it->second : nullptr;
}

const RenderComponent* Scene::render(EntityId id) const {
    auto it = renders_.find(id);
    return it != renders_.end() ? &it->second : nullptr;
}

const SelectionComponent* Scene::selection(EntityId id) const {
    auto it = selections_.find(id);
    return it != selections_.end() ? &it->second : nullptr;
}

const BoundsComponent* Scene::bounds(EntityId id) const {
    auto it = bounds_.find(id);
    return it != bounds_.end() ? &it->second : nullptr;
}

const LightComponent* Scene::light(EntityId id) const {
    auto it = lights_.find(id);
    return it != lights_.end() ? &it->second : nullptr;
}

bool Scene::setName(EntityId id, std::string name) {
    auto* c = mutableName(id);
    if (!c)
        return false;
    if (c->value == name)
        return true;

    c->value = std::move(name);
    markDirty(id, EntityDirty::Name);
    return true;
}

bool Scene::setLocalTransform(EntityId id, const math::Mat4& transform) {
    auto* c = mutableTransform(id);
    if (!c)
        return false;

    c->local = transform;
    updateWorldRecursive(id);
    return true;
}

bool Scene::setWorldTransform(EntityId id, const math::Mat4& transform) {
    auto* c = mutableTransform(id);
    if (!c)
        return false;

    c->world = transform;
    // 由 world 反推 local：local = parent.world⁻¹ * world（根节点 local = world）
    auto* h = mutableHierarchy(id);
    if (h && h->parent) {
        const auto* pt = this->transform(h->parent);
        c->local = pt ? pt->world.inverse() * transform : transform;
    } else {
        c->local = transform;
    }
    // world 已直接给定，子树仍需级联（本节点 world 是权威，子节点重算）
    updateWorldRecursive(id, /*selfWorldFixed=*/true);
    return true;
}

bool Scene::setGeometry(EntityId id, asset::AssetId geometry) {
    auto* c = mutableGeometry(id);
    if (!c)
        return false;
    if (c->geometry == geometry)
        return true;

    c->geometry = geometry;
    markDirty(id, EntityDirty::Geometry | EntityDirty::Bounds);
    return true;
}

bool Scene::setVisible(EntityId id, bool visible) {
    auto* c = mutableRender(id);
    if (!c)
        return false;
    if (c->visible == visible)
        return true;

    c->visible = visible;
    markDirty(id, EntityDirty::RenderState);
    return true;
}

bool Scene::setMaterialSlots(EntityId id, std::vector<asset::AssetId> materials) {
    auto* c = mutableRender(id);
    if (!c)
        return false;
    if (c->material_slots == materials)
        return true;

    c->material_slots = std::move(materials);
    markDirty(id, EntityDirty::Material);
    return true;
}

bool Scene::setSelected(EntityId id, bool selected) {
    auto* c = mutableSelection(id);
    if (!c)
        return false;
    if (c->selected == selected)
        return true;

    c->selected = selected;
    markDirty(id, EntityDirty::Selection);
    return true;
}

bool Scene::clearSelection() {
    bool changed = false;
    for (auto id : entities_) {
        if (auto* c = mutableSelection(id); c && c->selected) {
            c->selected = false;
            markDirty(id, EntityDirty::Selection);
            changed = true;
        }
    }
    return changed;
}

bool Scene::selectSingle(EntityId id) {
    bool changed = clearSelection();
    if (!isValid(id)) {
        return changed;
    }

    auto* c = mutableSelection(id);
    if (!c) {
        return changed;
    }

    if (!c->selected) {
        c->selected = true;
        markDirty(id, EntityDirty::Selection);
        changed = true;
    }
    return changed;
}

bool Scene::setWorldBounds(EntityId id, const math::AABB3& bounds) {
    auto* c = mutableBounds(id);
    if (!c)
        return false;

    c->world_bounds = bounds;
    markDirty(id, EntityDirty::Bounds);
    return true;
}

bool Scene::setLight(EntityId id, const LightComponent& light) {
    if (!isValid(id))
        return false;

    lights_[id] = light;
    markDirty(id, EntityDirty::Light);
    return true;
}

bool Scene::removeLight(EntityId id) {
    if (!isValid(id))
        return false;

    const bool removed = lights_.erase(id) > 0;
    if (removed) {
        markDirty(id, EntityDirty::Light);
    }
    return removed;
}

void Scene::markDirty(EntityId id, EntityDirty dirty) {
    dirty_[id] |= dirtyValue(dirty);
}

void Scene::updateWorldRecursive(EntityId id, bool selfWorldFixed) {
    if (!isValid(id))
        return;
    auto* c = mutableTransform(id);
    if (!c)
        return;

    // 本节点 world = parent.world * local（selfWorldFixed 时本节点 world 已是权威）
    if (!selfWorldFixed) {
        auto* h = mutableHierarchy(id);
        if (h && h->parent) {
            const auto* pt = transform(h->parent);
            c->world = pt ? pt->world * c->local : c->local;
        } else {
            c->world = c->local;
        }
    }
    markDirty(id, EntityDirty::Transform | EntityDirty::Bounds);

    // 递归子节点：它们的 world 依赖本节点 world
    auto childIt = children_.find(id);
    if (childIt != children_.end()) {
        // 拷贝一份，递归过程中 children_ 不会被改动（仅读）
        for (auto childId : childIt->second) {
            updateWorldRecursive(childId, false);
        }
    }
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
    if (child == parent)
        return true;

    EntityId cursor = parent;
    while (cursor) {
        if (cursor == child)
            return true;
        auto* h = hierarchy(cursor);
        if (!h)
            break;
        cursor = h->parent;
    }
    return false;
}

NameComponent* Scene::mutableName(EntityId id) {
    auto it = names_.find(id);
    return it != names_.end() ? &it->second : nullptr;
}

TransformComponent* Scene::mutableTransform(EntityId id) {
    auto it = transforms_.find(id);
    return it != transforms_.end() ? &it->second : nullptr;
}

HierarchyComponent* Scene::mutableHierarchy(EntityId id) {
    auto it = hierarchies_.find(id);
    return it != hierarchies_.end() ? &it->second : nullptr;
}

GeometryComponent* Scene::mutableGeometry(EntityId id) {
    auto it = geometries_.find(id);
    return it != geometries_.end() ? &it->second : nullptr;
}

RenderComponent* Scene::mutableRender(EntityId id) {
    auto it = renders_.find(id);
    return it != renders_.end() ? &it->second : nullptr;
}

SelectionComponent* Scene::mutableSelection(EntityId id) {
    auto it = selections_.find(id);
    return it != selections_.end() ? &it->second : nullptr;
}

BoundsComponent* Scene::mutableBounds(EntityId id) {
    auto it = bounds_.find(id);
    return it != bounds_.end() ? &it->second : nullptr;
}

LightComponent* Scene::mutableLight(EntityId id) {
    auto it = lights_.find(id);
    return it != lights_.end() ? &it->second : nullptr;
}

void Scene::addChild(EntityId parent, EntityId child) {
    children_[parent].push_back(child);
}

void Scene::removeChild(EntityId parent, EntityId child) {
    auto it = children_.find(parent);
    if (it == children_.end())
        return;

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
    lights_.erase(id);
}

}  // namespace mulan::scene
