/**
 * @file editor_session.cpp
 * @brief EditorSession 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "editor_session.h"

#include "selection_extrude_tool.h"

#include "grip_drag_tool.h"
#include "snap_marker_builder.h"
#include "transform_tool.h"
#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/math/math.h>
#include <mulan/view/core/view_context.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace mulan::app {

namespace {

bool isLeftPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Left;
}

bool hasTransformableEntitySubject(const TransformEditContext& context) {
    for (const TransformEditSubject& subject : context.subjects()) {
        if (subject.valid() && subject.hasInitialWorldTransform) {
            return true;
        }
    }
    return false;
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
    pick_service_.bind(session_, view_, binding_);
    overlay_service_.bind(view_);
    operation_executor_.bind(session_, binding_);
    grip_controller_.bind(session_, view_, &overlay_service_);
    selection_service_.bind(view_);
    refreshGrips();
    selection_service_.syncVisualState();
}

void EditorSession::unbind() {
    cancelActiveTool();
    clearGrips();
    selection_service_.unbind();
    grip_controller_.unbind();
    pick_service_.unbind();
    overlay_service_.unbind();
    operation_executor_.unbind();
    session_ = nullptr;
    view_ = nullptr;
    binding_ = nullptr;
}

bool EditorSession::isReady() const {
    return session_ != nullptr && view_ != nullptr && view_->isInitialized() && binding_ != nullptr;
}

void EditorSession::startTool(std::unique_ptr<EditorTool> tool) {
    overlay_service_.clear(EditorOverlayRole::Snap);
    clearGrips();
    applyAction(tool_controller_.start(std::move(tool)));
}

bool EditorSession::startTransformTool(TransformEditCommitMode commitMode) {
    if (!canStartTransformTool(commitMode)) {
        return false;
    }

    TransformEditContext context =
            TransformEditContext::fromSelection(*session_->document(), selection_service_.selected());
    if (!hasTransformableEntitySubject(context)) {
        return false;
    }

    startTool(std::make_unique<TransformTool>(session_->document(), std::move(context), TransformEditMode::Translate,
                                              commitMode));
    return true;
}

bool EditorSession::canStartTransformTool(TransformEditCommitMode commitMode) const {
    (void) commitMode;
    if (!isReady() || !session_ || !session_->document() || selection_service_.empty()) {
        return false;
    }

    const TransformEditContext context =
            TransformEditContext::fromSelection(*session_->document(), selection_service_.selected());
    return hasTransformableEntitySubject(context);
}

bool EditorSession::startSelectionExtrudeTool() {
    if (!isReady() || !session_ || !session_->document()) {
        return false;
    }
    startTool(std::make_unique<SelectionExtrudeTool>(*session_->document()));
    return true;
}

bool EditorSession::deleteSelectedEntities() {
    if (!isReady() || !binding_ || selection_service_.empty()) {
        return false;
    }

    std::vector<scene::EntityId> entities;
    for (const EditorSelectionReference& selected : selection_service_.selected()) {
        if (selected.entity && std::find(entities.begin(), entities.end(), selected.entity) == entities.end()) {
            entities.push_back(selected.entity);
        }
    }
    if (entities.empty()) {
        return false;
    }

    cancelActiveTool();
    const bool changed = operation_executor_.execute(DocumentOperation::removeEntities(std::move(entities), true));
    if (changed) {
        selection_service_.clear();
        binding_->clearSelection();
        refreshGrips();
    }
    return changed;
}

bool EditorSession::undo() {
    if (!isReady()) {
        return false;
    }

    cancelActiveTool();
    const bool changed = operation_executor_.undo();
    if (changed) {
        refreshGrips();
        selection_service_.syncVisualState();
    }
    return changed;
}

bool EditorSession::redo() {
    if (!isReady()) {
        return false;
    }

    cancelActiveTool();
    const bool changed = operation_executor_.redo();
    if (changed) {
        refreshGrips();
        selection_service_.syncVisualState();
    }
    return changed;
}

bool EditorSession::handleInput(const engine::InputEvent& event) {
    if (!view_) {
        return false;
    }

    if (!tool_controller_.hasActiveTool()) {
        return tryStartGripDrag(event);
    }

    EditorInput input = makeEditorInput(event);
    updateSnapPreview(input);

    EditorAction action = tool_controller_.handleInput(input);
    const ToolLifecycle lifecycle = action.lifecycle();
    const bool consumed = applyAction(std::move(action));
    if (lifecycle != ToolLifecycle::Running && view_) {
        overlay_service_.clear(EditorOverlayRole::Snap);
        refreshGrips();
    }
    return consumed;
}

void EditorSession::cancelActiveTool() {
    applyAction(tool_controller_.cancel());
    overlay_service_.clearAll();
    refreshGrips();
}

void EditorSession::refreshGrips() {
    grip_controller_.refresh(selection_service_.context(), isReady() && !tool_controller_.hasActiveTool());
}

void EditorSession::clearGrips() {
    grip_controller_.clear();
}

bool EditorSession::updateGripHoverAtFramebuffer(double screenX, double screenY) {
    if (!isReady() || tool_controller_.hasActiveTool()) {
        clearGripHover();
        return false;
    }

    return grip_controller_.updateHoverAtFramebuffer(screenX, screenY);
}

void EditorSession::clearGripHover() {
    grip_controller_.clearHover();
}

bool EditorSession::updateHoverAtFramebuffer(double screenX, double screenY) {
    if (!isReady()) {
        clearHover();
        return false;
    }

    if (updateGripHoverAtFramebuffer(screenX, screenY)) {
        selection_service_.clearHover();
        return true;
    }

    if (tool_controller_.hasActiveTool()) {
        clearHover();
        return false;
    }

    const std::optional<EditorSelectionHit> hit = pick_service_.selectionHitAtFramebuffer(screenX, screenY);
    selection_service_.setHovered(hit);
    return hit.has_value();
}

void EditorSession::selectAtFramebuffer(double screenX, double screenY) {
    if (!isReady() || !binding_) {
        return;
    }

    const std::optional<EditorSelectionHit> hit = pick_service_.selectionHitAtFramebuffer(screenX, screenY);
    if (hit) {
        selection_service_.selectSingleAndHover(*hit);
        binding_->selectSingle(hit->reference.entity);
    } else {
        selection_service_.clearSelectionAndHover();
        binding_->clearSelection();
    }
    refreshGrips();
}

void EditorSession::clearHover() {
    clearGripHover();
    selection_service_.clearHover();
}

void EditorSession::setSelectionFilter(EditorSelectionFilter filter) {
    selection_service_.setFilter(filter);
    refreshGrips();
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

    EditorInputResolveContext context;
    context.camera = &view_->camera();
    if (const EditorTool* tool = tool_controller_.activeTool()) {
        context.pointPolicy = tool->pointPolicy();
        context.snapSettings = tool->snapSettings();
    }

    const EditorPickInput pickInput = pick_service_.inputPick(event);
    context.pickTested = pickInput.tested;
    context.pickHit = pickInput.hit;
    context.pickWorld = pickInput.pickWorld;

    return input_resolver_.resolve(event, context);
}

void EditorSession::updateSnapPreview(const EditorInput& input) {
    overlay_service_.submit(EditorOverlaySubmission(EditorOverlayRole::Snap, SnapMarkerBuilder::build(input)));
}

bool EditorSession::tryStartGripDrag(const engine::InputEvent& event) {
    if (!isLeftPress(event)) {
        return false;
    }

    const std::optional<EditorGrip> grip =
            grip_controller_.pickAtFramebuffer(static_cast<double>(event.x), static_cast<double>(event.y));
    if (!grip) {
        return false;
    }

    const math::Point3 dragStart = grip->worldPosition;
    clearGrips();
    overlay_service_.clear(EditorOverlayRole::Snap);

    applyAction(tool_controller_.start(std::make_unique<GripDragTool>(*grip, dragStart)));
    return true;
}

bool EditorSession::applyAction(EditorAction action) {
    const bool consumed = action.isConsumed();

    if (action.shouldClearPreview()) {
        overlay_service_.clear(EditorOverlayRole::Tool);
    }

    if (action.preview()) {
        overlay_service_.submit(EditorOverlaySubmission(EditorOverlayRole::Tool, std::move(*action.preview())));
    }

    if (action.hasPreviewReferences()) {
        overlay_service_.submit(
                EditorOverlayReferenceSubmission(EditorOverlayRole::Tool, std::move(action.previewReferences())));
    }

    if (action.operation()) {
        if (operation_executor_.execute(std::move(*action.operation()))) {
            refreshGrips();
            selection_service_.syncVisualState();
        }
    }

    return consumed;
}

}  // namespace mulan::app
