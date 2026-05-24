/**
 * @file Entity.h
 * @brief 实体 — Document 中的数据记录
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 每个 Entity 有全局唯一 ID、名称、几何数据、变换、可见性。
 * 父子关系通过 EntityId 引用，不使用裸指针。
 */
#pragma once

#include "DocumentExport.h"
#include "EntityId.h"
#include "Geometry.h"

#include "mulan/Engine/Math/Math.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace mulan::document {

class DOCUMENT_API Entity {
public:
    /// 元数据值类型
    using Metadata = std::variant<std::string, double, int64_t, bool>;

    Entity(EntityId id, std::string name);

    // 禁止拷贝
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    // --- 标识 ---

    EntityId id() const { return m_id; }
    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    // --- 几何数据 ---

    Geometry* geometry() { return m_geometry.get(); }
    const Geometry* geometry() const { return m_geometry.get(); }
    void setGeometry(std::unique_ptr<Geometry> geo) { m_geometry = std::move(geo); }

    /// 是否拥有几何数据
    bool hasGeometry() const { return m_geometry != nullptr; }

    // --- 变换 ---

    const engine::Mat4& localTransform() const { return m_transform; }
    void setLocalTransform(const engine::Mat4& t) { m_transform = t; }

    // --- 可见性 ---

    bool visible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }

    // --- 父子关系（通过 EntityId 引用）---

    EntityId parentId() const { return m_parentId; }
    void setParentId(EntityId id) { m_parentId = id; }
    bool hasParent() const { return m_parentId.valid(); }

    // --- 元数据 ---

    void setMetadata(std::string key, Metadata value);
    const Metadata* getMetadata(const std::string& key) const;
    bool removeMetadata(const std::string& key);
    const auto& allMetadata() const { return m_metadata; }

private:
    EntityId                           m_id;
    std::string                        m_name;
    std::unique_ptr<Geometry>          m_geometry;
    engine::Mat4                       m_transform{1.0}; // identity
    bool                               m_visible = true;
    EntityId                           m_parentId;
    std::unordered_map<std::string, Metadata> m_metadata;
};

} // namespace mulan::Document
