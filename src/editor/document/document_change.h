/**
 * @file document_change.h
 * @brief 定义文档事务提交后发布的结构化变更戳。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 变更戳只描述已经成功提交的事实，不携带可变 Document 引用。渲染绑定、属性面板等
 * 消费者据此独立失效，修改者无需直接依赖具体视图对象。
 */
#pragma once
#ifndef MULAN_EDITOR_DOCUMENT_CHANGE_H
#define MULAN_EDITOR_DOCUMENT_CHANGE_H

#include <cstdint>

namespace mulan::editor {

enum class DocumentChangeKind : uint8_t {
    None = 0,
    Scene = 1u << 0u,
    Assets = 1u << 1u,
    VisualState = 1u << 2u,
};

constexpr DocumentChangeKind operator|(DocumentChangeKind lhs, DocumentChangeKind rhs) {
    return static_cast<DocumentChangeKind>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr DocumentChangeKind& operator|=(DocumentChangeKind& lhs, DocumentChangeKind rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasDocumentChange(DocumentChangeKind value, DocumentChangeKind expected) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(expected)) != 0;
}

struct DocumentChangeStamp {
    uint64_t revision = 0;
    DocumentChangeKind kinds = DocumentChangeKind::None;

    bool valid() const { return revision != 0 && kinds != DocumentChangeKind::None; }
    bool affectsContent() const {
        return hasDocumentChange(kinds, DocumentChangeKind::Scene) ||
               hasDocumentChange(kinds, DocumentChangeKind::Assets);
    }
    bool affectsVisualState() const { return hasDocumentChange(kinds, DocumentChangeKind::VisualState); }
};

}  // namespace mulan::editor

#endif  // MULAN_EDITOR_DOCUMENT_CHANGE_H
