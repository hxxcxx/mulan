#include "command_history.h"

#include <utility>

namespace mulan::editor {

void CommandHistory::clear() {
    undo_stack_.clear();
    redo_stack_.clear();
}

void CommandHistory::record(DocumentOperation redoOperation, DocumentOperation undoOperation) {
    undo_stack_.push_back(Entry{ std::move(redoOperation), std::move(undoOperation) });
    redo_stack_.clear();
}

void CommandHistory::recordIrreversibleChange() {
    // 仅清 redo 仍允许旧 undo 越过不可逆修改，可能把过期实体 ID 重放到新文档状态。
    clear();
}

std::optional<CommandHistory::Entry> CommandHistory::takeUndo() {
    if (undo_stack_.empty()) {
        return std::nullopt;
    }

    Entry entry = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    return entry;
}

void CommandHistory::restoreUndo(Entry entry) {
    undo_stack_.push_back(std::move(entry));
}

void CommandHistory::pushRedo(Entry entry) {
    redo_stack_.push_back(std::move(entry));
}

std::optional<CommandHistory::Entry> CommandHistory::takeRedo() {
    if (redo_stack_.empty()) {
        return std::nullopt;
    }

    Entry entry = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    return entry;
}

void CommandHistory::restoreRedo(Entry entry) {
    redo_stack_.push_back(std::move(entry));
}

void CommandHistory::pushUndo(Entry entry) {
    undo_stack_.push_back(std::move(entry));
}

}  // namespace mulan::editor
