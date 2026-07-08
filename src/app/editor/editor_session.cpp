/**
 * @file editor_session.cpp
 * @brief EditorSession 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "editor_session.h"

#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>
#include <mulan/math/math.h>
#include <mulan/view/preview_layer.h>
#include <mulan/view/view_context.h>

#include <string>
#include <utility>

namespace mulan::app {

namespace {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

}  // namespace

EditorSession::EditorSession() = default;

EditorSession::~EditorSession() {
    cancelActiveTool();
}

void EditorSession::bind(DocumentSession* session, view::ViewContext* view, DocumentViewBinding* binding) {
    cancelActiveTool();
    session_ = session;
    view_ = view;
    binding_ = binding;
}

void EditorSession::unbind() {
    cancelActiveTool();
    session_ = nullptr;
    view_ = nullptr;
    binding_ = nullptr;
}

bool EditorSession::isReady() const {
    return session_ != nullptr && view_ != nullptr && view_->isInitialized() && binding_ != nullptr;
}

void EditorSession::startTool(std::unique_ptr<EditorTool> tool) {
    applyAction(tool_controller_.start(std::move(tool)));
}

bool EditorSession::handleInput(const engine::InputEvent& event) {
    if (!tool_controller_.hasActiveTool() || !view_) {
        return false;
    }

    const bool consumed = applyAction(tool_controller_.handleInput(makeEditorInput(event)));
    return consumed;
}

void EditorSession::cancelActiveTool() {
    applyAction(tool_controller_.cancel());
}

void EditorSession::setWorkPlane(engine::WorkPlane plane) {
    input_resolver_.setWorkPlane(std::move(plane));
}

const engine::WorkPlane& EditorSession::workPlane() const {
    return input_resolver_.workPlane();
}

EditorInput EditorSession::makeEditorInput(const engine::InputEvent& event) const {
    if (!view_) {
        EditorInput input;
        input.event = event;
        return input;
    }

    return input_resolver_.resolve(event, view_->camera());
}

bool EditorSession::applyAction(EditorAction action) {
    const bool consumed = action.isConsumed();

    if (action.shouldClearPreview() && view_) {
        view_->clearPreview();
    }

    if (action.preview() && view_) {
        view_->previewLayer().setGeometry(action.preview()->takeCurves(), action.preview()->takeMeshes());
    }

    if (action.operation()) {
        applyOperation(std::move(*action.operation()));
    }

    return consumed;
}

bool EditorSession::applyOperation(DocumentOperation operation) {
    if (!session_ || !session_->document()) {
        return false;
    }

    io::DocumentEditor editor(*session_->document());
    bool changed = false;
    std::visit(Overloaded{
                       [&editor, &changed](CreateCurveOperation& create) {
                           changed = static_cast<bool>(
                                   editor.createCurve(std::move(create.name), std::move(create.primitive)));
                       },
                       [&editor, &changed](CreateMeshOperation& create) {
                           changed = static_cast<bool>(
                                   editor.createMesh(std::move(create.name), std::move(create.primitives)));
                       },
               },
               operation.data());

    if (changed && binding_) {
        binding_->refresh();
    }
    return changed;
}

}  // namespace mulan::app
