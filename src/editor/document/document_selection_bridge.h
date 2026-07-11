/**
 * @file document_selection_bridge.h
 * @brief DocumentSelectionBridge 封装文档选择变更与视图刷新。
 *
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include <mulan/scene/entity_id.h>

class DocumentSession;

namespace mulan::editor {

class DocumentRenderBinding;

class DocumentSelectionBridge {
public:
    void bind(DocumentSession& session, DocumentRenderBinding& renderBinding);
    void unbind();
    bool isBound() const { return session_ && render_binding_; }

    bool selectSingle(scene::EntityId entity);
    bool clearSelection();

private:
    DocumentSession* session_ = nullptr;
    DocumentRenderBinding* render_binding_ = nullptr;
};

}  // namespace mulan::editor
