/**
 * @file scene.h
 * @brief Scene —— 面向编辑器文档视图的组件化场景实例模型
 * @author hxxcxx
 * @date 2026-07-02 (原始) / 2026-07-15 (变更 journal 与实体 generation 边界)
 */

#pragma once

#include "components/geometry_component.h"
#include "components/hierarchy_component.h"
#include "components/light_component.h"
#include "components/name_component.h"
#include "components/render_component.h"
#include "components/selection_component.h"
#include "components/transform_component.h"
#include "entity_dirty.h"
#include "entity_id.h"
#include "scene_change.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mulan::scene {

class Scene {
public:
    static constexpr size_t DefaultChangeJournalCapacity = 4096;

    explicit Scene(size_t changeJournalCapacity = DefaultChangeJournalCapacity);

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

    const LightComponent* light(EntityId id) const;

    bool setName(EntityId id, std::string name);
    bool setLocalTransform(EntityId id, const math::Mat4& transform);
    bool setWorldTransform(EntityId id, const math::Mat4& transform);
    bool setGeometry(EntityId id, asset::AssetId geometry);
    bool setVisible(EntityId id, bool visible);
    bool setMaterialSlots(EntityId id, std::vector<asset::AssetId> materials);
    bool setSelected(EntityId id, bool selected);
    bool clearSelection();
    bool selectSingle(EntityId id);
    bool setLight(EntityId id, const LightComponent& light);
    bool removeLight(EntityId id);

    SceneRevision revision() const { return revision_; }
    SceneChangeDomain changeDomain() const { return change_domain_; }
    SceneChangeCursor currentChangeCursor() const { return SceneChangeCursor{ change_domain_, revision_ }; }
    size_t changeJournalCapacity() const { return change_journal_capacity_; }

    /// 非破坏查询 cursor 之后的变更，不自动确认消费进度。消费者成功应用增量或
    /// 完成全量重建后，才把自己的 cursor.revision 提交为结果的 toRevision。
    /// 若变更已被有界日志淘汰，返回 FullResyncRequired 且不返回残缺的增量。
    SceneChangeSet readChanges(const SceneChangeCursor& cursor) const;

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
    /// 每次 world 变更都发布 Transform，供 RenderScene 增量同步消费。
    void updateWorldRecursive(EntityId id, bool selfWorldFixed = false, bool forceSelfDirty = false);

    /// 仅供 Scene 自己的受控 mutator 发布；生命周期位不能由外部伪造。
    void markDirty(EntityId id, EntityDirty dirty);
    void appendChange(EntityId id, EntityDirty dirty);

    NameComponent* mutableName(EntityId id);
    TransformComponent* mutableTransform(EntityId id);
    HierarchyComponent* mutableHierarchy(EntityId id);
    GeometryComponent* mutableGeometry(EntityId id);
    RenderComponent* mutableRender(EntityId id);
    SelectionComponent* mutableSelection(EntityId id);
    LightComponent* mutableLight(EntityId id);

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
    std::unordered_map<EntityId, LightComponent> lights_;

    size_t change_journal_capacity_ = DefaultChangeJournalCapacity;
    SceneChangeDomain change_domain_ = 0;
    SceneRevision revision_ = 0;
    std::deque<SceneChange> change_journal_;
};

}  // namespace mulan::scene
