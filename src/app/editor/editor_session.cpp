/**
 * @file editor_session.cpp
 * @brief EditorSession 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "editor_session.h"

#include "grip_drag_tool.h"
#include "snap_marker_builder.h"
#include "transform_tool.h"
#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/math/math.h>
#include <mulan/engine/render/frontend/selection_visual_state.h>
#include <mulan/view/view_context.h>

#include <optional>
#include <utility>

namespace mulan::app {

namespace {

bool isLeftPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Left;
}

engine::SelectionVisualDomain visualDomain(EditorSelectionDomain domain, EditorSubEntityKind kind) {
    switch (domain) {
    case EditorSelectionDomain::Curve:
        switch (kind) {
        case EditorSubEntityKind::CurveSegment: return engine::SelectionVisualDomain::CurveSegment;
        case EditorSubEntityKind::CurveVertex: return engine::SelectionVisualDomain::CurveVertex;
        case EditorSubEntityKind::CurveElement: return engine::SelectionVisualDomain::CurveElement;
        case EditorSubEntityKind::Entity: return engine::SelectionVisualDomain::Entity;
        case EditorSubEntityKind::MeshFace:
        case EditorSubEntityKind::MeshEdge:
        case EditorSubEntityKind::MeshVertex:
        case EditorSubEntityKind::SurfaceFace:
        case EditorSubEntityKind::SurfaceEdge:
        case EditorSubEntityKind::SurfaceVertex:
        case EditorSubEntityKind::SolidFace:
        case EditorSubEntityKind::SolidEdge:
        case EditorSubEntityKind::SolidVertex: return engine::SelectionVisualDomain::Entity;
        case EditorSubEntityKind::ControlPoint: return engine::SelectionVisualDomain::ControlPoint;
        case EditorSubEntityKind::Grip: return engine::SelectionVisualDomain::Grip;
        }
        break;
    case EditorSelectionDomain::Mesh:
        switch (kind) {
        case EditorSubEntityKind::MeshFace: return engine::SelectionVisualDomain::MeshFace;
        case EditorSubEntityKind::MeshEdge: return engine::SelectionVisualDomain::MeshEdge;
        case EditorSubEntityKind::MeshVertex: return engine::SelectionVisualDomain::MeshVertex;
        case EditorSubEntityKind::Entity:
        case EditorSubEntityKind::CurveElement:
        case EditorSubEntityKind::CurveSegment:
        case EditorSubEntityKind::CurveVertex:
        case EditorSubEntityKind::SurfaceFace:
        case EditorSubEntityKind::SurfaceEdge:
        case EditorSubEntityKind::SurfaceVertex:
        case EditorSubEntityKind::SolidFace:
        case EditorSubEntityKind::SolidEdge:
        case EditorSubEntityKind::SolidVertex:
        case EditorSubEntityKind::ControlPoint:
        case EditorSubEntityKind::Grip: return engine::SelectionVisualDomain::Entity;
        }
        break;
    case EditorSelectionDomain::Surface:
        if (kind == EditorSubEntityKind::MeshFace || kind == EditorSubEntityKind::SurfaceFace) {
            return engine::SelectionVisualDomain::SurfaceFace;
        }
        if (kind == EditorSubEntityKind::MeshEdge || kind == EditorSubEntityKind::SurfaceEdge) {
            return engine::SelectionVisualDomain::SurfaceEdge;
        }
        if (kind == EditorSubEntityKind::MeshVertex || kind == EditorSubEntityKind::SurfaceVertex) {
            return engine::SelectionVisualDomain::SurfaceVertex;
        }
        break;
    case EditorSelectionDomain::Solid:
        if (kind == EditorSubEntityKind::MeshFace || kind == EditorSubEntityKind::SolidFace) {
            return engine::SelectionVisualDomain::SolidFace;
        }
        if (kind == EditorSubEntityKind::MeshEdge || kind == EditorSubEntityKind::SolidEdge) {
            return engine::SelectionVisualDomain::SolidEdge;
        }
        if (kind == EditorSubEntityKind::MeshVertex || kind == EditorSubEntityKind::SolidVertex) {
            return engine::SelectionVisualDomain::SolidVertex;
        }
        break;
    case EditorSelectionDomain::Entity: break;
    }
    return engine::SelectionVisualDomain::Entity;
}

engine::SelectionVisualTarget visualTarget(const EditorSelectionReference& reference,
                                           engine::SelectionVisualRole role) {
    engine::SelectionVisualTarget target;
    target.pickId = reference.renderPickId();
    target.role = role;
    target.domain = visualDomain(reference.domain, reference.kind);

    if (reference.subObject.hasSourceDrawableIndex) {
        target.sourceDrawableIndex = static_cast<uint32_t>(reference.subObject.sourceDrawableIndex);
        target.hasSourceDrawableIndex = true;
    }
    if (reference.subObject.hasPrimitiveIndex) {
        target.primitiveIndex = static_cast<uint32_t>(reference.subObject.primitiveIndex);
        target.hasPrimitiveIndex = true;
    }
    if (reference.subObject.hasComponentIndex) {
        target.componentIndex = static_cast<uint32_t>(reference.subObject.componentIndex);
        target.hasComponentIndex = true;
    }
    return target;
}

bool hasMovableEntitySubject(const TransformEditContext& context) {
    for (const TransformEditSubject& subject : context.subjects()) {
        if (subject.wholeEntity() && subject.hasInitialWorldTransform) {
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
    preview_controller_.bind(view_);
    operation_executor_.bind(session_, binding_);
    grip_controller_.bind(session_, view_, &preview_controller_);
    refreshGrips();
    syncSelectionVisualState();
}

void EditorSession::unbind() {
    cancelActiveTool();
    clearGrips();
    selection_context_.clear();
    if (view_) {
        view_->clearSelectionVisualState();
    }
    grip_controller_.unbind();
    pick_service_.unbind();
    preview_controller_.unbind();
    operation_executor_.unbind();
    session_ = nullptr;
    view_ = nullptr;
    binding_ = nullptr;
}

bool EditorSession::isReady() const {
    return session_ != nullptr && view_ != nullptr && view_->isInitialized() && binding_ != nullptr;
}

void EditorSession::startTool(std::unique_ptr<EditorTool> tool) {
    preview_controller_.clearSnapGeometry();
    clearGrips();
    applyAction(tool_controller_.start(std::move(tool)));
}

bool EditorSession::startTransformTool(TransformEditCommitMode commitMode) {
    if (!canStartTransformTool(commitMode)) {
        return false;
    }

    TransformEditContext context =
            TransformEditContext::fromSelection(*session_->document(), selection_context_.selected());
    if (!hasMovableEntitySubject(context)) {
        return false;
    }

    startTool(std::make_unique<TransformTool>(session_->document(), std::move(context), TransformEditMode::Translate,
                                              commitMode));
    return true;
}

bool EditorSession::canStartTransformTool(TransformEditCommitMode commitMode) const {
    (void) commitMode;
    if (!isReady() || !session_ || !session_->document() || selection_context_.empty()) {
        return false;
    }

    const TransformEditContext context =
            TransformEditContext::fromSelection(*session_->document(), selection_context_.selected());
    return hasMovableEntitySubject(context);
}

bool EditorSession::undo() {
    if (!isReady()) {
        return false;
    }

    cancelActiveTool();
    const bool changed = operation_executor_.undo();
    if (changed) {
        refreshGrips();
        syncSelectionVisualState();
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
        syncSelectionVisualState();
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
        preview_controller_.clearSnapGeometry();
        refreshGrips();
    }
    return consumed;
}

void EditorSession::cancelActiveTool() {
    applyAction(tool_controller_.cancel());
    preview_controller_.clearAll();
    refreshGrips();
}

void EditorSession::refreshGrips() {
    grip_controller_.refresh(selection_context_, isReady() && !tool_controller_.hasActiveTool());
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
        selection_context_.clearHover();
        if (view_) {
            view_->clearHoveredPickId();
        }
        syncSelectionVisualState();
        return true;
    }

    if (tool_controller_.hasActiveTool()) {
        clearHover();
        return false;
    }

    const std::optional<EditorSelectionHit> hit = pick_service_.selectionHitAtFramebuffer(screenX, screenY);
    selection_context_.setHovered(hit);
    if (view_) {
        if (hit) {
            view_->setHoveredPickId(hit->reference.renderPickId());
        } else {
            view_->clearHoveredPickId();
        }
    }
    syncSelectionVisualState();
    return hit.has_value();
}

void EditorSession::selectAtFramebuffer(double screenX, double screenY) {
    if (!isReady() || !binding_) {
        return;
    }

    const std::optional<EditorSelectionHit> hit = pick_service_.selectionHitAtFramebuffer(screenX, screenY);
    if (hit) {
        selection_context_.selectSingle(*hit);
        selection_context_.setHovered(hit);
        if (view_) {
            view_->setHoveredPickId(hit->reference.renderPickId());
        }
        syncSelectionVisualState();
        binding_->selectSingle(hit->reference.entity);
    } else {
        selection_context_.clearSelection();
        selection_context_.clearHover();
        if (view_) {
            view_->clearHoveredPickId();
        }
        syncSelectionVisualState();
        binding_->clearSelection();
    }
    refreshGrips();
}

void EditorSession::clearHover() {
    clearGripHover();
    selection_context_.clearHover();
    if (view_) {
        view_->clearHoveredPickId();
    }
    syncSelectionVisualState();
}

void EditorSession::setSelectionFilter(EditorSelectionFilter filter) {
    selection_context_.setFilter(filter);
    selection_context_.clearHover();
    syncSelectionVisualState();
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
    if (pickInput.renderScene) {
        context.renderScene = pickInput.renderScene;
    }

    return input_resolver_.resolve(event, context);
}

void EditorSession::updateSnapPreview(const EditorInput& input) {
    preview_controller_.setSnapGeometry(SnapMarkerBuilder::build(input));
}

void EditorSession::syncSelectionVisualState() {
    if (!view_) {
        return;
    }

    engine::SelectionVisualState state;
    state.setActive(true);
    for (const EditorSelectionReference& selected : selection_context_.selected()) {
        state.add(visualTarget(selected, engine::SelectionVisualRole::Selected));
    }
    if (const auto& hovered = selection_context_.hovered(); hovered && hovered->valid()) {
        state.add(visualTarget(hovered->reference, engine::SelectionVisualRole::Hovered));
    }
    view_->setSelectionVisualState(std::move(state));
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
    preview_controller_.clearSnapGeometry();

    applyAction(tool_controller_.start(std::make_unique<GripDragTool>(*grip, dragStart)));
    return true;
}

bool EditorSession::applyAction(EditorAction action) {
    const bool consumed = action.isConsumed();

    if (action.shouldClearPreview()) {
        preview_controller_.clearToolGeometry();
    }

    if (action.preview()) {
        preview_controller_.setToolGeometry(std::move(*action.preview()));
    }

    if (action.operation()) {
        if (operation_executor_.execute(std::move(*action.operation()))) {
            refreshGrips();
            syncSelectionVisualState();
        }
    }

    return consumed;
}

}  // namespace mulan::app
