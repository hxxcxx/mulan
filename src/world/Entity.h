/**
 * @file Entity.h
 * @brief Entity — World 中的领域实体，固定属性 + 多态 GeometryData
 *
 * 设计原则：
 * - Entity 不派生，所有实体共享同一套固定属性
 * - 几何数据通过 GeometryData（多态）表达差异化
 * - Id 高 32 位 = generation，低 32 位 = index（防止悬挂）
 * - Parent 关系通过 Id 引用，children 索引由 World 维护
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "EntityDirty.h"

#include "mulan/engine/math/Math.h"
#include "mulan/engine/math/AABB.h"

#include <cstdint>
#include <string>

namespace mulan::world {

class Entity {
public:
    using Id = uint64_t;

    static constexpr int      INDEX_BITS = 32;
    static constexpr uint32_t INDEX_MASK = ~uint32_t(0);

    static constexpr Id INVALID_ID = 0;

    Entity(Id id, std::string name);

    // 禁止拷贝
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    // --- 标识 ---

    Id id() const { return m_id; }
    uint32_t index() const { return static_cast<uint32_t>(m_id & INDEX_MASK); }
    uint32_t generation() const { return static_cast<uint32_t>(m_id >> INDEX_BITS); }

    /// World 销毁 Entity 后，旧句柄 generation 不匹配 → valid() 返回 false
    bool valid(const class World& world) const;

    const std::string& name() const { return m_name; }
    void setName(std::string name);

    // --- 变换 ---

    const engine::Mat4& localTransform() const { return m_localTransform; }
    void setLocalTransform(const engine::Mat4& t);

    const engine::Mat4& worldTransform() const { return m_worldTransform; }
    void setWorldTransform(const engine::Mat4& t) { m_worldTransform = t; }

    // --- 父子关系 ---

    Id parentId() const { return m_parent; }
    bool setParentId(Id parentId);

    // --- 可见性 ---

    bool visible() const { return m_visible; }
    void setVisible(bool v);

    // --- 选中 ---

    bool selected() const { return m_selected; }
    void setSelected(bool s);

    // --- 材质 ---

    uint16_t materialId() const { return m_materialId; }
    void setMaterialId(uint16_t id);

    // --- 脏标记（由 Entity setter 自动设置，World 消费）---

    uint64_t dirtyFlags() const { return m_dirtyFlags; }
    bool isDirty(EntityDirty d) const { return hasDirty(m_dirtyFlags, d); }

    // --- 包围盒 ---

    const engine::AABB& cachedBounds() const { return m_cachedBounds; }
    void setCachedBounds(const engine::AABB& b) { m_cachedBounds = b; }

private:
    friend class World;

    void markDirty(EntityDirty d) { m_dirtyFlags |= static_cast<uint64_t>(d); }
    void clearDirtyInternal(EntityDirty d) { m_dirtyFlags &= ~static_cast<uint64_t>(d); }

    Id              m_id;
    std::string     m_name;
    engine::Mat4    m_localTransform{1.0};
    engine::Mat4    m_worldTransform{1.0};
    Id              m_parent{INVALID_ID};
    bool            m_visible = true;
    bool            m_selected = false;
    uint16_t        m_materialId = 0xFFFF;
    uint64_t        m_dirtyFlags = 0;
    engine::AABB    m_cachedBounds;
};

} // namespace mulan::world
