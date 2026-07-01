/**
 * @file macro_command.h
 * @brief 组合命令 — 将多个命令当作一个原子操作
 * @author hxxcxx
 * @date 2026-05-25
 *
 * 用法：批量移动、粘贴（删除 + 创建）、导入（多次 createEntity）等
 */
#pragma once

#include "command.h"

#include <memory>
#include <string>
#include <vector>

namespace mulan::command {

class COMMAND_API MacroCommand : public Command {
public:
    explicit MacroCommand(std::string cmdName)
        : name_(std::move(cmdName)) {}

    /// 添加子命令
    void addCommand(std::unique_ptr<Command> cmd) {
        commands_.push_back(std::move(cmd));
    }

    bool execute(world::World& world) override {
        for (auto& cmd : commands_) {
            if (!cmd->execute(world))
                return false;
        }
        return true;
    }

    std::string name() const override { return name_; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};

} // namespace mulan::command
