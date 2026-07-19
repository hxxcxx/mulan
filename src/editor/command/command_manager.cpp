/**
 * @file command_manager.cpp
 * @brief CommandManager 实现。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "command_manager.h"

#include "../core/session/editor_session.h"
#include "../document/document_view.h"

namespace mulan::editor {

bool CommandManager::add(std::unique_ptr<Command> command) {
    if (!command || command->id().empty()) {
        return false;
    }

    const std::string id(command->id());
    return commands_.emplace(id, std::move(command)).second;
}

Command* CommandManager::find(std::string_view id) const {
    const auto it = commands_.find(std::string(id));
    return it != commands_.end() ? it->second.get() : nullptr;
}

CommandState CommandManager::state(std::string_view id, const CommandHost& host) const {
    const Command* command = find(id);
    if (!command) {
        return CommandState{
            .title = std::string(id),
            .statusText = "Command not found",
            .enabled = false,
        };
    }
    return command->state(host);
}

CommandOutcome CommandManager::execute(std::string_view id, CommandHost host) {
    Command* command = find(id);
    if (!command) {
        return std::unexpected(Error::make(ErrorCode::NotFound, "Command not found"));
    }

    const CommandState currentState = command->state(host);
    if (currentState.visible && currentState.enabled && currentState.checkable && currentState.checked) {
        EditorSession* editor = host.editorSession();
        if (editor && editor->activeToolId() == id) {
            editor->cancelActiveTool();
            if (DocumentView* view = host.documentView()) {
                view->onCommandCompleted();
            }
            return {};
        }
    }

    CommandOutcome result = command->execute(host);
    if (result) {
        if (DocumentView* view = host.documentView()) {
            view->onCommandCompleted();
        }
    }
    return result;
}

}  // namespace mulan::editor
