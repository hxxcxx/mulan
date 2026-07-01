/**
 * @file command_manager.h
 * @brief 命令管理器 — 注册、执行、历史记录
 * @author hxxcxx
 * @date 2026-05-25
 *
 * 设计思路：
 *  - 唯一持有命令所有权的入口
 *  - 支持按名称注册工厂（QAction 绑定用）
 *  - 支持直接传入命令对象（Operator 交互完成后用）
 *  - 防重入：同一时间只允许一个命令执行
 *  - 记录历史（为未来 undo/redo 预留）
 */
#pragma once

#include "command_export.h"
#include "command.h"
#include "macro_command.h"

#include <mulan/world/World.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mulan::command {

class COMMAND_API CommandManager {
public:
    explicit CommandManager(world::World& world)
        : world_(world) {}

    // --- 命令注册 ---

    using CommandFactory = std::function<std::unique_ptr<Command>()>;

    /// 注册命令工厂（按名称查找）
    void registerCommand(std::string name, CommandFactory factory) {
        factories_[name] = std::move(factory);
    }

    // --- 执行 ---

    /// 按注册名执行（QAction / 快捷键 绑定用）
    bool execute(const std::string& name) {
        auto it = factories_.find(name);
        if (it == factories_.end()) return false;
        auto cmd = it->second();
        if (!cmd) return false;
        return executeCommand(std::move(cmd));
    }

    /// 直接传入命令对象（交互完成后用）
    bool execute(std::unique_ptr<Command> cmd) {
        return executeCommand(std::move(cmd));
    }

    // --- 宏命令 ---

    void beginMacro(std::string name) {
        active_macro_ = std::make_unique<MacroCommand>(std::move(name));
    }

    void addToMacro(std::unique_ptr<Command> cmd) {
        if (active_macro_)
            active_macro_->addCommand(std::move(cmd));
    }

    bool endMacro() {
        if (!active_macro_) return false;
        return executeCommand(std::move(active_macro_));
    }

    // --- 状态 ---

    size_t historyCount() const { return history_.size(); }
    void clearHistory() { history_.clear(); }

    /// 当前是否有命令正在执行（防重入）
    bool isExecuting() const { return executing_; }

    // --- 通知 ---
    // 未来可替换为信号/回调机制

    std::function<void()> onDocumentChanged;

private:
    bool executeCommand(std::unique_ptr<Command> cmd) {
        if (!cmd || executing_) return false;

        executing_ = true;
        bool ok = cmd->execute(world_);
        executing_ = false;

        if (ok) {
            history_.push_back(std::move(cmd));
            if (onDocumentChanged) onDocumentChanged();
        }
        return ok;
    }

    world::World& world_;
    std::unordered_map<std::string, CommandFactory> factories_;
    std::vector<std::unique_ptr<Command>> history_;
    std::unique_ptr<MacroCommand> active_macro_;
    bool executing_ = false;
};

} // namespace mulan::command
