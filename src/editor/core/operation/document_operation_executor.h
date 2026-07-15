/**
 * @file document_operation_executor.h
 * @brief DocumentOperationExecutor 将编辑器文档操作应用到当前文档会话。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_operation.h"

#include <optional>

class DocumentSession;
class DocumentViewBinding;

namespace mulan::editor {

class CommandHistory;

class DocumentOperationExecutor {
public:
    void bind(DocumentSession* session, DocumentViewBinding* binding);
    void unbind();

    bool isBound() const { return session_ != nullptr; }
    bool execute(DocumentOperation operation);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearHistory();

private:
    struct ApplyResult {
        bool changed = false;
        std::optional<DocumentOperation> undoOperation;
    };

    ApplyResult apply(DocumentOperation operation) const;
    bool applyWithoutRecording(DocumentOperation operation) const;
    bool refreshAfterChange(bool changed) const;

    DocumentSession* session_ = nullptr;
    DocumentViewBinding* binding_ = nullptr;
    /// 非拥有指针：由当前 DocumentSession 持有，unbind 只解除借用。
    CommandHistory* history_ = nullptr;
};

}  // namespace mulan::editor
