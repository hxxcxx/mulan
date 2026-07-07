/**
 * @file builtin_commands.cpp
 * @brief 应用内置命令实现。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "builtin_commands.h"

#include "command_manager.h"

#include "editor/line_tool.h"

#include "ui/document_view.h"

#include <memory>

namespace mulan::app {
namespace {

class FitAllCommand final : public Command {
public:
    std::string_view id() const override { return "view.fitAll"; }
    std::string_view title() const override { return "Fit All"; }

protected:
    CommandOutcome perform(CommandHost& host) override {
        DocumentView* view = host.documentView();
        if (!view || !view->isInitialized() || !view->session()) {
            return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active document view"));
        }

        view->binding().fitAll();
        return {};
    }
};

class DrawLineCommand final : public Command {
public:
    std::string_view id() const override { return "draw.line"; }
    std::string_view title() const override { return "Line"; }

protected:
    CommandOutcome perform(CommandHost& host) override {
        DocumentView* view = host.documentView();
        if (!view || !view->isInitialized() || !view->session()) {
            return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active document view"));
        }

        view->startTool(std::make_unique<LineTool>());
        return {};
    }
};

}  // namespace

void registerBuiltinCommands(CommandManager& manager) {
    manager.add(std::make_unique<FitAllCommand>());
    manager.add(std::make_unique<DrawLineCommand>());
}

}  // namespace mulan::app
