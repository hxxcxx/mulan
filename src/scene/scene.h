#pragma once

#include "components/bounds_component.h"
#include "components/geometry_component.h"
#include "components/hierarchy_component.h"
#include "components/name_component.h"
#include "components/render_component.h"
#include "components/selection_component.h"
#include "components/transform_component.h"
#include "entity_dirty.h"
#include "entity_id.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::scene {

class Scene {
public:
    Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    EntityId createEntity(std::string name = {});
    void destroyEntity(EntityId id);

    bool isValid(EntityId id) const;
    size_t entityCount() const { return entities_.size(); }

    bool setParent(EntityId child, EntityId parent);
    const std::vector<EntityId>& childrenOf(EntityId parent) const;

    NameComponent* name(EntityId id);
    const NameComponent* name(EntityId id) const;

    TransformComponent* transform(EntityId id);
    const TransformComponent* transform(EntityId id) const;

    HierarchyComponent* hierarchy(EntityId id);
    const HierarchyComponent* hierarchy(EntityId id) const;

    GeometryComponent* geometry(EntityId id);
    const GeometryComponent* geometry(EntityId id) const;

    RenderComponent* render(EntityId id);
    const RenderComponent* render(EntityId id) const;

    SelectionComponent* selection(EntityId id);
    const SelectionComponent* selection(EntityId id) const;

    BoundsComponent* bounds(EntityId id);
    const BoundsComponent* bounds(EntityId id) const;

    void markDirty(EntityId id, EntityDirty dirty);
    uint64_t dirtyFlags(EntityId id) const;
    void clearDirty(EntityDirty mask);

    template<typename Func>
    void forEachEntity(Func&& fn) {
        for (auto id : entities_)
            fn(id);
    }

    template<typename Func>
    void forEachDirty(EntityDirty mask, Func&& fn) {
        uint64_t maskValue = dirtyValue(mask);
        for (auto& [id, flags] : dirty_) {
            if (flags & maskValue)
                fn(id, flags);
        }
    }

private:
    struct Slot {
        uint32_t generation = 1;
        bool alive = false;
    };

    EntityId allocateId();
    uint32_t indexOf(EntityId id) const { return id.index(); }
    bool detectCycle(EntityId child, EntityId parent) const;

    void addChild(EntityId parent, EntityId child);
    void removeChild(EntityId parent, EntityId child);
    void eraseComponents(EntityId id);

    std::vector<Slot> slots_;
    std::vector<EntityId> free_indices_;
    std::vector<EntityId> entities_;
    std::unordered_map<EntityId, std::vector<EntityId>> children_;

    std::unordered_map<EntityId, NameComponent> names_;
    std::unordered_map<EntityId, TransformComponent> transforms_;
    std::unordered_map<EntityId, HierarchyComponent> hierarchies_;
    std::unordered_map<EntityId, GeometryComponent> geometries_;
    std::unordered_map<EntityId, RenderComponent> renders_;
    std::unordered_map<EntityId, SelectionComponent> selections_;
    std::unordered_map<EntityId, BoundsComponent> bounds_;
    std::unordered_map<EntityId, uint64_t> dirty_;
};

} // namespace mulan::scene

