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

std::optional<math::Point3> mapOrthographicScreenToWorldXY(const engine::Camera& camera,
                                                           const engine::InputEvent& event) {
    if (!camera.isOrthographic() || camera.width() <= 0 || camera.height() <= 0) {
        return std::nullopt;
    }

    const double ndcX = (2.0 * static_cast<double>(event.x)) / static_cast<double>(camera.width()) - 1.0;
    const double ndcY = 1.0 - (2.0 * static_cast<double>(event.y)) / static_cast<double>(camera.height());
    const double halfHeight = camera.orthoSize();
    const double halfWidth = halfHeight * camera.aspect();
    const math::Vec3 target = camera.target();

    return math::Point3(target.x + ndcX * halfWidth, target.y + ndcY * halfHeight, 0.0);
}

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

    return applyAction(tool_controller_.handleInput(makeEditorInput(event)));
}

void EditorSession::cancelActiveTool() {
    applyAction(tool_controller_.cancel());
}

EditorInput EditorSession::makeEditorInput(const engine::InputEvent& event) const {
    EditorInput input;
    input.event = event;

    if (!view_) {
        return input;
    }

    const engine::Camera& camera = view_->camera();
    input.cursorRay = camera.screenRay(event.x, event.y);
    input.workPlane = math::Plane3::fromPointNormal(math::Point3::origin(), math::Vec3::unitZ());

    if (auto point = mapOrthographicScreenToWorldXY(camera, event)) {
        input.workPoint = *point;
        return input;
    }

    const math::Hit3 hit = math::intersect(input.cursorRay, input.workPlane);
    if (hit.hit) {
        input.workPoint = hit.point;
    }

    return input;
}

bool EditorSession::applyAction(EditorAction action) {
    const bool consumed = action.isConsumed();

    if (action.shouldClearPreview() && view_) {
        view_->clearPreview();
    }

    if (action.preview() && view_) {
        view_->previewLayer().setCurves(action.preview()->takeCurves());
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
    std::visit(
            Overloaded{
                    [&editor, &changed](CreateCurveOperation& create) {
                        changed = static_cast<bool>(
                                editor.createCurve(std::move(create.name), std::move(create.primitive)));
                    },
            },
            operation.data());

    if (changed && binding_) {
        binding_->refresh();
    }
    return changed;
}

}  // namespace mulan::app
