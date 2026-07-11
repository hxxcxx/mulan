/**
 * @file command_manager.cpp
 * @brief CommandManager 实现。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "command_manager.h"

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
        return std::unexpected(core::Error::make(core::ErrorCode::NotFound, "Command not found"));
    }

    return command->execute(host);
}

}  // namespace mulan::editor
