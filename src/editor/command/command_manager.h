/**
 * @file command_manager.h
 * @brief 管理应用命令列表并按 id 执行命令。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include "command.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mulan::editor {

class CommandManager {
public:
    bool add(std::unique_ptr<Command> command);

    Command* find(std::string_view id) const;
    CommandState state(std::string_view id, const CommandHost& host) const;
    CommandOutcome execute(std::string_view id, CommandHost host);

private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;
};

}  // namespace mulan::editor
