#include "world.h"
#include "system/system.h"

namespace mulan::world {

Entity::Id World::allocateId() {
    uint32_t idx;
    uint32_t gen;
    if (!free_indices_.empty()) {
        idx = free_indices_.back();
        free_indices_.pop_back();
    } else {
        idx = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }
    gen = slots_[idx].generation;
    slots_[idx].alive = true;
    return (static_cast<uint64_t>(gen) << Entity::INDEX_BITS) | idx;
}

Entity* World::createEntity(std::string name) {
    Entity::Id id = allocateId();
    auto entity = std::make_unique<Entity>(id, std::move(name));
    Entity* ptr = entity.get();
    ptr->setWorldPtr(this);
    // 在 m_world 就绪后标 Created，确保 dirty 正确传入 World 的 dirty map
    ptr->markDirty(EntityDirty::Created);
    entities_[id] = std::move(entity);
    return ptr;
}

void World::destroyEntity(Entity::Id id) {
    // 提升 children
    auto it = children_.find(id);
    if (it != children_.end()) {
        auto children = std::move(it->second);
        children_.erase(it);
        for (auto childId : children) {
            Entity* child = entityById(childId);
            if (child) {
                child->parent_ = Entity::INVALID_ID;
                child->markDirty(EntityDirty::Parent | EntityDirty::Transform);
            }
        }
    }

    // 从 parent 的 children 列表中移除
    Entity* e = entityById(id);
    if (e && e->parentId() != Entity::INVALID_ID) {
        removeChild(e->parentId(), id);
    }

    markDirty(id, EntityDirty::Destroyed);

    uint32_t idx = indexOf(id);
    slots_[idx].alive = false;
    slots_[idx].generation++;
    free_indices_.push_back(static_cast<Entity::Id>(idx));

    entities_.erase(id);
}

Entity* World::entityById(Entity::Id id) {
    auto it = entities_.find(id);
    return it != entities_.end() ? it->second.get() : nullptr;
}

const Entity* World::entityById(Entity::Id id) const {
    auto it = entities_.find(id);
    return it != entities_.end() ? it->second.get() : nullptr;
}

void World::addChild(Entity::Id parentId, Entity::Id childId) {
    children_[parentId].push_back(childId);
}

void World::removeChild(Entity::Id parentId, Entity::Id childId) {
    auto it = children_.find(parentId);
    if (it != children_.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), childId), vec.end());
        if (vec.empty()) children_.erase(it);
    }
}

const std::vector<Entity::Id>& World::childrenOf(Entity::Id parentId) const {
    static const std::vector<Entity::Id> empty;
    auto it = children_.find(parentId);
    return it != children_.end() ? it->second : empty;
}

void World::markDirty(Entity::Id id, EntityDirty d) {
    dirty_[id] |= dirtyValue(d);
}

uint64_t World::getDirtyFlags(Entity::Id id) const {
    auto it = dirty_.find(id);
    return it != dirty_.end() ? it->second : 0;
}

void World::clearDirty(EntityDirty mask) {
    uint64_t keep = ~dirtyValue(mask);
    for (auto it = dirty_.begin(); it != dirty_.end();) {
        it->second &= keep;
        if (it->second == 0)
            it = dirty_.erase(it);
        else
            ++it;
    }
}

World::~World() = default;

bool World::isValid(Entity::Id id) const {
    if (id == Entity::INVALID_ID) return false;
    uint32_t idx = static_cast<uint32_t>(id & Entity::INDEX_MASK);
    if (idx >= slots_.size()) return false;
    return slots_[idx].alive && (id >> Entity::INDEX_BITS) == slots_[idx].generation;
}

bool World::setParent(Entity::Id childId, Entity::Id parentId) {
    Entity* child = entityById(childId);
    if (!child) return false;

    // 允许解除 parent
    if (parentId == Entity::INVALID_ID) {
        if (child->parentId() != Entity::INVALID_ID) {
            removeChild(child->parentId(), childId);
        }
        child->setParentId(Entity::INVALID_ID);
        child->markDirty(EntityDirty::Parent | EntityDirty::Transform);
        return true;
    }

    // 目标 parent 必须有效
    Entity* parent = entityById(parentId);
    if (!parent) return false;

    // 循环检测
    if (detectCycle(childId, parentId)) return false;

    // 从旧 parent 移出
    if (child->parentId() != Entity::INVALID_ID) {
        removeChild(child->parentId(), childId);
    }

    // 注册到新 parent
    child->setParentId(parentId);
    addChild(parentId, childId);
    child->markDirty(EntityDirty::Parent | EntityDirty::Transform);
    return true;
}

bool World::detectCycle(Entity::Id childId, Entity::Id parentId) const {
    if (childId == parentId) return true;
    Entity::Id cursor = parentId;
    while (cursor != Entity::INVALID_ID) {
        if (cursor == childId) return true;
        const Entity* e = entityById(cursor);
        if (!e) break;
        cursor = e->parentId();
    }
    return false;
}

void World::addSystem(std::unique_ptr<System> sys) {
    systems_.push_back(std::move(sys));
    std::sort(systems_.begin(), systems_.end(),
              [](const auto& a, const auto& b) { return a->priority() < b->priority(); });
}

void World::updateLogic(float dt) {
    for (auto& sys : systems_) {
        sys->update(*this, dt);
    }
}

} // namespace mulan::world
