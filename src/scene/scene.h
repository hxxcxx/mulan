/**
 * @file scene.h
 * @brief Scene —— 面向编辑器文档视图的组件化场景实例模型
 * @author hxxcxx
 * @date 2026-07-02
 */

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
#include <utility>
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

    const NameComponent* name(EntityId id) const;

    const TransformComponent* transform(EntityId id) const;

    const HierarchyComponent* hierarchy(EntityId id) const;

    const GeometryComponent* geometry(EntityId id) const;

    const RenderComponent* render(EntityId id) const;

    const SelectionComponent* selection(EntityId id) const;

    const BoundsComponent* bounds(EntityId id) const;

    bool setName(EntityId id, std::string name);
    bool setLocalTransform(EntityId id, const math::Mat4& transform);
    bool setWorldTransform(EntityId id, const math::Mat4& transform);
    bool setGeometry(EntityId id, asset::AssetId geometry);
    bool setVisible(EntityId id, bool visible);
    bool setMaterialSlots(EntityId id, std::vector<asset::AssetId> materials);
    bool setSelected(EntityId id, bool selected);
    bool setWorldBounds(EntityId id, const math::AABB3& bounds);

    void markDirty(EntityId id, EntityDirty dirty);
    uint64_t dirtyFlags(EntityId id) const;
    void clearDirty(EntityDirty mask);

    template <typename Func>
    void forEachEntity(Func&& fn) {
        for (auto id : entities_)
            fn(id);
    }

    template <typename Func>
    void forEachEntity(Func&& fn) const {
        for (auto id : entities_)
            fn(id);
    }

    template <typename Func>
    void forEachDirty(EntityDirty mask, Func&& fn) {
        uint64_t maskValue = dirtyValue(mask);
        for (auto& [id, flags] : dirty_) {
            if (flags & maskValue)
                fn(id, flags);
        }
    }

    template <typename Func>
    void forEachDirty(EntityDirty mask, Func&& fn) const {
        uint64_t maskValue = dirtyValue(mask);
        for (const auto& [id, flags] : dirty_) {
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

    /// 自 id 起重算 world 矩阵并递归到整个子树。
    /// world = parent.world * local；selfWorldFixed=true 时本节点 world 已由调用方
    /// 设定（如 setWorldTransform），跳过本节点重算只级联子节点。
    /// 每次 world 变更都 markDirty(Transform|Bounds)，供 RenderScene 增量同步消费。
    void updateWorldRecursive(EntityId id, bool selfWorldFixed = false);

    NameComponent* mutableName(EntityId id);
    TransformComponent* mutableTransform(EntityId id);
    HierarchyComponent* mutableHierarchy(EntityId id);
    GeometryComponent* mutableGeometry(EntityId id);
    RenderComponent* mutableRender(EntityId id);
    SelectionComponent* mutableSelection(EntityId id);
    BoundsComponent* mutableBounds(EntityId id);

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

}  // namespace mulan::scene
