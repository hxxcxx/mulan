/**
 * @file MacroCommand.h
 * @brief 组合命令 — 将多个命令当作一个原子操作
 * @author hxxcxx
 * @date 2026-05-25
 *
 * 用法：批量移动、粘贴（删除 + 创建）、导入（多次 createEntity）等
 */
#pragma once

#include "Command.h"

#include <memory>
#include <string>
#include <vector>

namespace mulan::command {

class COMMAND_API MacroCommand : public Command {
public:
    explicit MacroCommand(std::string cmdName)
        : m_name(std::move(cmdName)) {}

    /// 添加子命令
    void addCommand(std::unique_ptr<Command> cmd) {
        m_commands.push_back(std::move(cmd));
    }

    bool execute(world::World& world) override {
        for (auto& cmd : m_commands) {
            if (!cmd->execute(world))
                return false;
        }
        return true;
    }

    std::string name() const override { return m_name; }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Command>> m_commands;
};

} // namespace mulan::command
