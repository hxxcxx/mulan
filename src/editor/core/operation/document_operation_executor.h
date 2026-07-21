/**
 * @file document_operation_executor.h
 * @brief DocumentOperationExecutor 将编辑器文档操作应用到当前文档会话。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_operation.h"
#include "../../document/document_change.h"

#include <optional>

namespace mulan::editor {

class DocumentSession;
class CommandHistory;

class DocumentOperationExecutor {
public:
    explicit DocumentOperationExecutor(DocumentSession& session);
    bool execute(DocumentOperation operation);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearHistory();

private:
    struct ApplyResult {
        bool changed = false;
        DocumentChangeKind changes = DocumentChangeKind::None;
        std::optional<DocumentOperation> undoOperation;
        std::vector<EntityIdRemap> entityRemaps;
        std::vector<AssetIdRemap> assetRemaps;
    };

    ApplyResult apply(DocumentOperation operation) const;
    bool publish(const ApplyResult& result) const;

    DocumentSession& session_;
    /// 非拥有引用：DocumentOperationExecutor 的生命周期包含在 DocumentSession 挂接期内。
    CommandHistory& history_;
};

}  // namespace mulan::editor
