/**
 * @file entity.h
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

#include "entity_dirty.h"
#include "geometry_data.h"

#include "mulan/engine/math/math.h"
#include "mulan/engine/math/aabb.h"

#include <cstdint>
#include <memory>
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

    Id id() const { return id_; }
    uint32_t index() const { return static_cast<uint32_t>(id_ & INDEX_MASK); }
    uint32_t generation() const { return static_cast<uint32_t>(id_ >> INDEX_BITS); }

    /// World 销毁 Entity 后，旧句柄 generation 不匹配 → valid() 返回 false
    bool valid(const class World& world) const;

    const std::string& name() const { return name_; }
    void setName(std::string name);

    // --- 变换 ---

    const engine::Mat4& localTransform() const { return local_transform_; }
    void setLocalTransform(const engine::Mat4& t);

    const engine::Mat4& worldTransform() const { return world_transform_; }
    void setWorldTransform(const engine::Mat4& t) { world_transform_ = t; }

    // --- 父子关系 ---

    Id parentId() const { return parent_; }
    /// 仅 World 可调用 setParentId，外部通过 World::setParent() 操作


    // --- 可见性 ---

    bool visible() const { return visible_; }
    void setVisible(bool v);

    // --- 选中 ---

    bool selected() const { return selected_; }
    void setSelected(bool s);

    // --- 材质 ---

    uint16_t materialId() const { return material_id_; }
    void setMaterialId(uint16_t id);

    // --- 颜色 ---

    const engine::Vec3& color() const { return color_; }
    void setColor(const engine::Vec3& c) { color_ = c; }

    // --- 几何数据 ---

    GeometryData* geometry() { return geometry_.get(); }
    const GeometryData* geometry() const { return geometry_.get(); }
    bool hasGeometry() const { return geometry_ != nullptr; }

    void setGeometry(std::unique_ptr<GeometryData> geo);

    /// 安全修改几何数据（先标脏再修改，清除 mesh 缓存）
    template<typename Func>
    void modifyGeometry(Func&& fn) {
        markDirty(EntityDirty::Geometry);
        cached_face_mesh_ = engine::Mesh{};
        cached_edge_mesh_ = engine::Mesh{};
        if (geometry_) fn(*geometry_);
    }

    // --- 惰性网格缓存 ---

    const engine::Mesh& cachedFaceMesh() const;
    const engine::Mesh& cachedEdgeMesh() const;

    // --- 脏标记（由 Entity setter 自动设置，World 消费）---

    uint64_t dirtyFlags() const { return dirty_flags_; }
    bool isDirty(EntityDirty d) const { return hasDirty(dirty_flags_, d); }

    // --- 包围盒 ---

    const engine::AABB& cachedBounds() const { return cached_bounds_; }
    void setCachedBounds(const engine::AABB& b) { cached_bounds_ = b; }

private:
    friend class World;

    void markDirty(EntityDirty d);
    void clearDirtyInternal(EntityDirty d) { dirty_flags_ &= ~static_cast<uint64_t>(d); }
    void setParentId(Id parentId) { parent_ = parentId; }
    void setWorldPtr(World* w) { world_ = w; }

    Id              id_;
    World*          world_ = nullptr;
    std::string     name_;
    std::unique_ptr<GeometryData> geometry_;
    engine::Mat4    local_transform_{1.0};
    engine::Mat4    world_transform_{1.0};
    Id              parent_{INVALID_ID};
    bool            visible_ = true;
    bool            selected_ = false;
    uint16_t        material_id_ = 0xFFFF;
    engine::Vec3    color_{1.0, 1.0, 1.0};
    uint64_t        dirty_flags_ = 0;
    engine::AABB    cached_bounds_;

    mutable engine::Mesh cached_face_mesh_;
    mutable engine::Mesh cached_edge_mesh_;
};

} // namespace mulan::world
