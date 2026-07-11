/**
 * @file document_operation_executor.h
 * @brief DocumentOperationExecutor 将编辑器文档操作应用到当前文档会话。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "core/operation/command_history.h"
#include "core/operation/document_operation.h"

#include <optional>

class DocumentSession;
class DocumentViewBinding;

namespace mulan::app {

class DocumentOperationExecutor {
public:
    void bind(DocumentSession* session, DocumentViewBinding* binding);
    void unbind();

    bool isBound() const { return session_ != nullptr; }
    bool execute(DocumentOperation operation);
    bool undo();
    bool redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
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
    CommandHistory history_;
};

}  // namespace mulan::app
