/**
 * @file document_selection_bridge.h
 * @brief DocumentSelectionBridge 封装文档选择变更与视图刷新。
 *
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include <mulan/scene/entity_id.h>

namespace mulan::editor {

class DocumentSession;

class DocumentSelectionBridge {
public:
    explicit DocumentSelectionBridge(DocumentSession& session) : session_(session) {}

    bool selectSingle(scene::EntityId entity);
    bool clearSelection();

private:
    DocumentSession& session_;
};

}  // namespace mulan::editor
