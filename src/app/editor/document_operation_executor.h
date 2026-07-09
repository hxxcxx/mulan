/**
 * @file document_operation_executor.h
 * @brief DocumentOperationExecutor 将编辑器文档操作应用到当前文档会话。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_operation.h"

#include <optional>
#include <vector>

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
    bool canUndo() const { return !undo_stack_.empty(); }
    bool canRedo() const { return !redo_stack_.empty(); }
    void clearHistory();

private:
    struct HistoryEntry {
        DocumentOperation redoOperation;
        DocumentOperation undoOperation;
    };

    struct ApplyResult {
        bool changed = false;
        std::optional<DocumentOperation> undoOperation;
    };

    ApplyResult apply(DocumentOperation operation) const;
    bool applyWithoutRecording(DocumentOperation operation) const;
    bool refreshAfterChange(bool changed) const;

    DocumentSession* session_ = nullptr;
    DocumentViewBinding* binding_ = nullptr;
    std::vector<HistoryEntry> undo_stack_;
    std::vector<HistoryEntry> redo_stack_;
};

}  // namespace mulan::app
