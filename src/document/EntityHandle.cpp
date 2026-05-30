/**
 * @file EntityHandle.cpp
 * @brief EntityHandle 实现
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "EntityHandle.h"
#include "Document.h"

namespace mulan::document {

EntityHandle::EntityHandle(EntityId id, Document* doc)
    : m_id(id)
    , m_doc(doc)
{}

Entity* EntityHandle::get() {
    if (!m_id.valid() || !m_doc) return nullptr;
    return m_doc->findEntity(m_id);
}

const Entity* EntityHandle::get() const {
    if (!m_id.valid() || !m_doc) return nullptr;
    return m_doc->findEntity(m_id);
}

bool EntityHandle::valid() const {
    return get() != nullptr;
}

} // namespace mulan::document
