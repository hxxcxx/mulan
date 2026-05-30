/**
 * @file EntityHandle.h
 * @brief 实体安全引用 — EntityId + Document 指针
 * @author hxxcxx
 * @date 2026-04-22
 *
 * EntityHandle 是跨系统引用实体的唯一方式。
 * 通过 id 在 document 中查找，不存在时返回 nullptr，
 * 避免裸指针悬挂问题。
 */
#pragma once

#include "DocumentExport.h"
#include "EntityId.h"

namespace mulan::document {

class Entity;
class Document;

class DOCUMENT_API EntityHandle {
public:
    EntityHandle() = default;
    EntityHandle(EntityId id, Document* doc);

    EntityId id() const { return m_id; }
    Document* document() const { return m_doc; }

    /// 安全访问 — 实体已被删除时返回 nullptr
    Entity* get();
    const Entity* get() const;

    /// 是否指向有效实体
    bool valid() const;

    /// 便捷访问
    Entity* operator->() { return get(); }
    const Entity* operator->() const { return get(); }
    Entity& operator*() { return *get(); }
    const Entity& operator*() const { return *get(); }

    explicit operator bool() const { return valid(); }

    bool operator==(const EntityHandle& o) const {
        return m_id == o.m_id && m_doc == o.m_doc;
    }
    bool operator!=(const EntityHandle& o) const { return !(*this == o); }

private:
    EntityId  m_id;
    Document* m_doc = nullptr;
};

} // namespace mulan::document
