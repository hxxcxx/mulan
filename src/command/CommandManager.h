/**
 * @file CommandManager.h
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

#include "CommandExport.h"
#include "Command.h"
#include "MacroCommand.h"

#include "mulan/document/Document.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mulan::command {

class COMMAND_API CommandManager {
public:
    explicit CommandManager(document::Document& doc)
        : m_doc(doc) {}

    // --- 命令注册 ---

    using CommandFactory = std::function<std::unique_ptr<Command>()>;

    /// 注册命令工厂（按名称查找）
    void registerCommand(std::string name, CommandFactory factory) {
        m_factories[name] = std::move(factory);
    }

    // --- 执行 ---

    /// 按注册名执行（QAction / 快捷键 绑定用）
    bool execute(const std::string& name) {
        auto it = m_factories.find(name);
        if (it == m_factories.end()) return false;
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
        m_activeMacro = std::make_unique<MacroCommand>(std::move(name));
    }

    void addToMacro(std::unique_ptr<Command> cmd) {
        if (m_activeMacro)
            m_activeMacro->addCommand(std::move(cmd));
    }

    bool endMacro() {
        if (!m_activeMacro) return false;
        return executeCommand(std::move(m_activeMacro));
    }

    // --- 状态 ---

    size_t historyCount() const { return m_history.size(); }
    void clearHistory() { m_history.clear(); }

    /// 当前是否有命令正在执行（防重入）
    bool isExecuting() const { return m_executing; }

    // --- 通知 ---
    // 未来可替换为信号/回调机制

    std::function<void()> onDocumentChanged;

private:
    bool executeCommand(std::unique_ptr<Command> cmd) {
        if (!cmd || m_executing) return false;

        m_executing = true;
        bool ok = cmd->execute(m_doc);
        m_executing = false;

        if (ok) {
            m_history.push_back(std::move(cmd));
            m_doc.setModified(true);
            if (onDocumentChanged) onDocumentChanged();
        }
        return ok;
    }

    document::Document& m_doc;
    std::unordered_map<std::string, CommandFactory> m_factories;
    std::vector<std::unique_ptr<Command>> m_history;
    std::unique_ptr<MacroCommand> m_activeMacro;
    bool m_executing = false;
};

} // namespace mulan::command
