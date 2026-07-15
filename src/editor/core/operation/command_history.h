/**
 * @file command_history.h
 * @brief CommandHistory 保存编辑命令的撤销与重做事务栈。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_operation.h"

#include <optional>
#include <vector>

namespace mulan::editor {

class CommandHistory {
public:
    struct Entry {
        DocumentOperation redoOperation;
        DocumentOperation undoOperation;
    };

    bool canUndo() const { return !undo_stack_.empty(); }
    bool canRedo() const { return !redo_stack_.empty(); }
    void clear();

    void record(DocumentOperation redoOperation, DocumentOperation undoOperation);
    /// 文档已发生不可撤销的修改，禁止撤销/重做跨越这个事务边界。
    void recordIrreversibleChange();

    std::optional<Entry> takeUndo();
    void restoreUndo(Entry entry);
    void pushRedo(Entry entry);

    std::optional<Entry> takeRedo();
    void restoreRedo(Entry entry);
    void pushUndo(Entry entry);

private:
    std::vector<Entry> undo_stack_;
    std::vector<Entry> redo_stack_;
};

}  // namespace mulan::editor
