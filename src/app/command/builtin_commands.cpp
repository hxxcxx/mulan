/**
 * @file builtin_commands.cpp
 * @brief 应用内置命令实现。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "builtin_commands.h"

#include "command_manager.h"

#include "editor/circle_tool.h"
#include "editor/editor_session.h"
#include "editor/face_tool.h"
#include "editor/line_tool.h"
#include "editor/polyline_tool.h"

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

template <typename Tool>
CommandOutcome startDrawTool(CommandHost& host) {
    EditorSession* editor = host.editorSession();
    if (!editor || !editor->isReady()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }

    if (DocumentView* view = host.documentView(); view && view->isInitialized()) {
        view->viewContext().setCameraToWorldXY();
    }
    editor->setWorkPlane(mulan::engine::WorkPlane::worldXY());
    editor->startTool(std::make_unique<Tool>());
    return {};
}

CommandOutcome startViewPlaneDrawTool(CommandHost& host) {
    EditorSession* editor = host.editorSession();
    DocumentView* view = host.documentView();
    if (!editor || !editor->isReady() || !view || !view->isInitialized()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }

    editor->setWorkPlane(mulan::engine::WorkPlane::fromView(view->viewContext().camera()));
    editor->startTool(std::make_unique<FaceTool>());
    return {};
}

class DrawLineCommand final : public Command {
public:
    std::string_view id() const override { return "draw.line"; }
    std::string_view title() const override { return "Line"; }

protected:
    CommandOutcome perform(CommandHost& host) override { return startDrawTool<LineTool>(host); }
};

class DrawPolylineCommand final : public Command {
public:
    std::string_view id() const override { return "draw.polyline"; }
    std::string_view title() const override { return "Polyline"; }

protected:
    CommandOutcome perform(CommandHost& host) override { return startDrawTool<PolylineTool>(host); }
};

class DrawCircleCommand final : public Command {
public:
    std::string_view id() const override { return "draw.circle"; }
    std::string_view title() const override { return "Circle"; }

protected:
    CommandOutcome perform(CommandHost& host) override { return startDrawTool<CircleTool>(host); }
};

class DrawFaceCommand final : public Command {
public:
    std::string_view id() const override { return "draw.face"; }
    std::string_view title() const override { return "Face"; }

protected:
    CommandOutcome perform(CommandHost& host) override { return startViewPlaneDrawTool(host); }
};

}  // namespace

void registerBuiltinCommands(CommandManager& manager) {
    manager.add(std::make_unique<FitAllCommand>());
    manager.add(std::make_unique<DrawLineCommand>());
    manager.add(std::make_unique<DrawPolylineCommand>());
    manager.add(std::make_unique<DrawCircleCommand>());
    manager.add(std::make_unique<DrawFaceCommand>());
}

}  // namespace mulan::app
