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
#include "editor/extrude_tool.h"
#include "editor/face_tool.h"
#include "editor/line_tool.h"
#include "editor/param_curve_tool.h"
#include "editor/polyline_tool.h"

#include "ui/document_session.h"
#include "ui/document_view.h"

#include <memory>
#include <string>
#include <utility>

namespace mulan::app {
namespace {

CommandState commandState(const Command& command, bool enabled, std::string statusText = {}) {
    std::string text = enabled ? std::string(command.statusText()) : std::move(statusText);
    return CommandState{
        .title = std::string(command.title()),
        .shortcut = std::string(command.shortcut()),
        .statusText = std::move(text),
        .enabled = enabled,
    };
}

bool hasReadyEditor(const CommandHost& host) {
    const EditorSession* editor = host.editorSession();
    return editor && editor->isReady();
}

const DocumentSession* documentSession(const CommandHost& host) {
    const DocumentView* view = host.documentView();
    return view ? view->session() : nullptr;
}

CommandState readyEditorState(const Command& command, const CommandHost& host) {
    return commandState(command, hasReadyEditor(host), "No active editor session");
}

CommandState hiddenDrawingState(const Command& command, std::string statusText) {
    CommandState state = commandState(command, false, std::move(statusText));
    state.visible = false;
    return state;
}

bool canUseDrawingCommands(const CommandHost& host) {
    const DocumentSession* session = documentSession(host);
    return session && session->allowsDrawingCommands();
}

CommandState drawingState(const Command& command, const CommandHost& host) {
    const DocumentSession* session = documentSession(host);
    if (!session) {
        return hiddenDrawingState(command, "No active draft document");
    }
    if (!session->allowsDrawingCommands()) {
        return hiddenDrawingState(command, "Drawing is unavailable for imported documents");
    }
    CommandState state = readyEditorState(command, host);
    state.checkable = true;
    const EditorSession* editor = host.editorSession();
    state.checked = editor && editor->activeToolId() == command.id();
    return state;
}

CommandState activeToolState(const Command& command, const CommandHost& host, CommandState state) {
    state.checkable = true;
    const EditorSession* editor = host.editorSession();
    state.checked = editor && editor->activeToolId() == command.id();
    return state;
}

CommandState transformState(const Command& command, const CommandHost& host, TransformEditCommitMode commitMode) {
    const EditorSession* editor = host.editorSession();
    if (!editor || !editor->isReady()) {
        return commandState(command, false, "No active editor session");
    }
    return commandState(command, editor->canStartTransformTool(commitMode), "No selected movable entity");
}

class FitAllCommand final : public Command {
public:
    std::string_view id() const override { return "view.fitAll"; }
    std::string_view title() const override { return "Fit All"; }
    std::string_view shortcut() const override { return "F"; }
    CommandState state(const CommandHost& host) const override {
        DocumentView* view = host.documentView();
        return commandState(*this, view && view->isInitialized() && view->session(), "No active document view");
    }

protected:
    CommandOutcome perform(CommandHost& host) override {
        DocumentView* view = host.documentView();
        if (!view || !view->isInitialized() || !view->session()) {
            return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active document view"));
        }

        view->fitAll();
        return {};
    }
};

template <typename Tool>
CommandOutcome startDrawTool(CommandHost& host) {
    EditorSession* editor = host.editorSession();
    if (!canUseDrawingCommands(host)) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "Drawing is unavailable for imported documents"));
    }
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

/// 固定在世界 XY 工作平面绘制，但不改变用户当前的观察视角。
template <typename Tool>
CommandOutcome startWorldXYDrawTool(CommandHost& host) {
    EditorSession* editor = host.editorSession();
    if (!canUseDrawingCommands(host)) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "Drawing is unavailable for imported documents"));
    }
    if (!editor || !editor->isReady()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }

    editor->setWorkPlane(mulan::engine::WorkPlane::worldXY());
    editor->startTool(std::make_unique<Tool>());
    return {};
}

CommandOutcome startParametricCurveTool(CommandHost& host, ParametricCurveToolKind kind) {
    EditorSession* editor = host.editorSession();
    if (!canUseDrawingCommands(host)) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "Drawing is unavailable for imported documents"));
    }
    if (!editor || !editor->isReady()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }

    if (DocumentView* view = host.documentView(); view && view->isInitialized()) {
        view->viewContext().setCameraToWorldXY();
    }
    editor->setWorkPlane(mulan::engine::WorkPlane::worldXY());
    editor->startTool(std::make_unique<ParametricCurveTool>(kind));
    return {};
}

template <typename Tool>
CommandOutcome startViewPlaneDrawTool(CommandHost& host) {
    EditorSession* editor = host.editorSession();
    DocumentView* view = host.documentView();
    if (!canUseDrawingCommands(host)) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "Drawing is unavailable for imported documents"));
    }
    if (!editor || !editor->isReady() || !view || !view->isInitialized()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }

    editor->setWorkPlane(mulan::engine::WorkPlane::fromView(view->viewContext().camera()));
    editor->startTool(std::make_unique<Tool>());
    return {};
}

CommandOutcome startTransformTool(CommandHost& host, TransformEditCommitMode commitMode) {
    EditorSession* editor = host.editorSession();
    if (!editor || !editor->isReady()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }
    if (!editor->startTransformTool(commitMode)) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No selected movable entity"));
    }
    return {};
}

CommandOutcome runUndo(CommandHost& host) {
    EditorSession* editor = host.editorSession();
    if (!editor || !editor->isReady()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }
    if (!editor->undo()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Nothing to undo"));
    }
    return {};
}

CommandOutcome runRedo(CommandHost& host) {
    EditorSession* editor = host.editorSession();
    if (!editor || !editor->isReady()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
    }
    if (!editor->redo()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Nothing to redo"));
    }
    return {};
}

class DrawLineCommand final : public Command {
public:
    std::string_view id() const override { return "draw.line"; }
    std::string_view title() const override { return "Line"; }
    CommandState state(const CommandHost& host) const override { return drawingState(*this, host); }

protected:
    CommandOutcome perform(CommandHost& host) override { return startDrawTool<LineTool>(host); }
};

class DrawPolylineCommand final : public Command {
public:
    std::string_view id() const override { return "draw.polyline"; }
    std::string_view title() const override { return "Polyline"; }
    CommandState state(const CommandHost& host) const override { return drawingState(*this, host); }

protected:
    CommandOutcome perform(CommandHost& host) override { return startDrawTool<PolylineTool>(host); }
};

class DrawCircleCommand final : public Command {
public:
    std::string_view id() const override { return "draw.circle"; }
    std::string_view title() const override { return "Circle"; }
    CommandState state(const CommandHost& host) const override { return drawingState(*this, host); }

protected:
    CommandOutcome perform(CommandHost& host) override { return startDrawTool<CircleTool>(host); }
};

class DrawFaceCommand final : public Command {
public:
    std::string_view id() const override { return "draw.face"; }
    std::string_view title() const override { return "Face"; }
    CommandState state(const CommandHost& host) const override { return drawingState(*this, host); }

protected:
    CommandOutcome perform(CommandHost& host) override { return startViewPlaneDrawTool<FaceTool>(host); }
};

class DrawExtrudeCommand final : public Command {
public:
    std::string_view id() const override { return "draw.extrude"; }
    std::string_view title() const override { return "Extrude"; }
    CommandState state(const CommandHost& host) const override {
        return hiddenDrawingState(*this, "Direct profile extrusion is hidden");
    }

protected:
    CommandOutcome perform(CommandHost& host) override { return startWorldXYDrawTool<ExtrudeTool>(host); }
};

class ModelExtrudeCommand final : public Command {
public:
    std::string_view id() const override { return "model.extrude"; }
    std::string_view title() const override { return "Extrude"; }
    CommandState state(const CommandHost& host) const override {
        return activeToolState(*this, host, readyEditorState(*this, host));
    }

protected:
    CommandOutcome perform(CommandHost& host) override {
        EditorSession* editor = host.editorSession();
        if (!editor || !editor->startSelectionExtrudeTool()) {
            return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No active editor session"));
        }
        return {};
    }
};

class DrawBezierCommand final : public Command {
public:
    std::string_view id() const override { return "draw.bezier"; }
    std::string_view title() const override { return "Bezier"; }
    CommandState state(const CommandHost& host) const override { return drawingState(*this, host); }

protected:
    CommandOutcome perform(CommandHost& host) override {
        return startParametricCurveTool(host, ParametricCurveToolKind::Bezier);
    }
};

class DrawBSplineCommand final : public Command {
public:
    std::string_view id() const override { return "draw.bspline"; }
    std::string_view title() const override { return "B-Spline"; }
    CommandState state(const CommandHost& host) const override { return drawingState(*this, host); }

protected:
    CommandOutcome perform(CommandHost& host) override {
        return startParametricCurveTool(host, ParametricCurveToolKind::BSpline);
    }
};

class DrawNurbsCommand final : public Command {
public:
    std::string_view id() const override { return "draw.nurbs"; }
    std::string_view title() const override { return "NURBS"; }
    CommandState state(const CommandHost& host) const override { return drawingState(*this, host); }

protected:
    CommandOutcome perform(CommandHost& host) override {
        return startParametricCurveTool(host, ParametricCurveToolKind::NURBS);
    }
};

class EditMoveCommand final : public Command {
public:
    std::string_view id() const override { return "edit.move"; }
    std::string_view title() const override { return "Move"; }
    CommandState state(const CommandHost& host) const override {
        return transformState(*this, host, TransformEditCommitMode::Move);
    }

protected:
    CommandOutcome perform(CommandHost& host) override {
        return startTransformTool(host, TransformEditCommitMode::Move);
    }
};

class EditCopyCommand final : public Command {
public:
    std::string_view id() const override { return "edit.copy"; }
    std::string_view title() const override { return "Copy"; }
    CommandState state(const CommandHost& host) const override {
        return transformState(*this, host, TransformEditCommitMode::Copy);
    }

protected:
    CommandOutcome perform(CommandHost& host) override {
        return startTransformTool(host, TransformEditCommitMode::Copy);
    }
};

class EditDeleteCommand final : public Command {
public:
    std::string_view id() const override { return "edit.delete"; }
    std::string_view title() const override { return "Delete"; }
    std::string_view shortcut() const override { return "Del"; }
    CommandState state(const CommandHost& host) const override {
        const EditorSession* editor = host.editorSession();
        return commandState(*this, editor && editor->isReady() && !editor->selectionContext().empty(),
                            "No selected entity");
    }

protected:
    CommandOutcome perform(CommandHost& host) override {
        EditorSession* editor = host.editorSession();
        if (!editor || !editor->deleteSelectedEntities()) {
            return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "No selected entity"));
        }
        return {};
    }
};

class EditUndoCommand final : public Command {
public:
    std::string_view id() const override { return "edit.undo"; }
    std::string_view title() const override { return "Undo"; }
    std::string_view shortcut() const override { return "Ctrl+Z"; }
    CommandState state(const CommandHost& host) const override {
        const EditorSession* editor = host.editorSession();
        if (!editor || !editor->isReady()) {
            return commandState(*this, false, "No active editor session");
        }
        return commandState(*this, editor->canUndo(), "Nothing to undo");
    }

protected:
    CommandOutcome perform(CommandHost& host) override { return runUndo(host); }
};

class EditRedoCommand final : public Command {
public:
    std::string_view id() const override { return "edit.redo"; }
    std::string_view title() const override { return "Redo"; }
    std::string_view shortcut() const override { return "Ctrl+Y"; }
    CommandState state(const CommandHost& host) const override {
        const EditorSession* editor = host.editorSession();
        if (!editor || !editor->isReady()) {
            return commandState(*this, false, "No active editor session");
        }
        return commandState(*this, editor->canRedo(), "Nothing to redo");
    }

protected:
    CommandOutcome perform(CommandHost& host) override { return runRedo(host); }
};

}  // namespace

void registerBuiltinCommands(CommandManager& manager) {
    manager.add(std::make_unique<FitAllCommand>());
    manager.add(std::make_unique<EditUndoCommand>());
    manager.add(std::make_unique<EditRedoCommand>());
    manager.add(std::make_unique<EditMoveCommand>());
    manager.add(std::make_unique<EditCopyCommand>());
    manager.add(std::make_unique<EditDeleteCommand>());
    manager.add(std::make_unique<DrawLineCommand>());
    manager.add(std::make_unique<DrawPolylineCommand>());
    manager.add(std::make_unique<DrawCircleCommand>());
    manager.add(std::make_unique<DrawFaceCommand>());
    manager.add(std::make_unique<DrawExtrudeCommand>());
    manager.add(std::make_unique<ModelExtrudeCommand>());
    manager.add(std::make_unique<DrawBezierCommand>());
    manager.add(std::make_unique<DrawBSplineCommand>());
    manager.add(std::make_unique<DrawNurbsCommand>());
}

}  // namespace mulan::app
