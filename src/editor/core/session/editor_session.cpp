/**
 * @file editor_session.cpp
 * @brief EditorSession 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "editor_session.h"

#include "../tools/selection_extrude_tool.h"

#include "../tools/editor_tool_operator.h"
#include "../tools/grip_drag_tool.h"
#include "../snap/snap_marker_builder.h"
#include "../tools/transform_tool.h"
#include "document/document_session.h"
#include "document/document_view_binding.h"

#include <mulan/math/math.h>
#include <mulan/core/log/log.h>
#include <mulan/view/core/view_context.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace mulan::editor {

namespace {

bool hasTransformableEntitySubject(const TransformEditContext& context) {
    for (const TransformEditSubject& subject : context.subjects()) {
        if (subject.valid() && subject.hasInitialWorldTransform) {
            return true;
        }
    }
    return false;
}

}  // namespace

EditorSession::EditorSession(DocumentSession& session, view::ViewContext& view, DocumentViewBinding& binding)
    : session_(session),
      view_(view),
      binding_(binding),
      pick_service_(session, view, binding),
      overlay_service_(view),
      operation_executor_(session),
      grip_controller_(session, view, overlay_service_),
      selection_service_(view) {
    refreshGrips();
    selection_service_.syncVisualState();
    LOG_DEBUG("[Editor] Editor session attached: document={}", session_.displayName());
}

EditorSession::~EditorSession() {
    release();
}

void EditorSession::release() {
    const std::string documentName = session_.displayName();
    cancelActiveTool();
    if (!documentName.empty()) {
        LOG_DEBUG("[Editor] Editor session detached: document={}", documentName);
    }
}

bool EditorSession::isReady() const {
    return view_.isReady();
}

void EditorSession::startTool(std::unique_ptr<EditorTool> tool) {
    // Operator 与工具是一一对应关系。先精确移除旧适配器，再让 ToolController
    // 以 Replaced 结束旧工具，避免栈中残留仍引用当前 Session 的 ghost operator。
    removeToolOperator();
    overlay_service_.clear(EditorOverlayRole::Snap);
    clearGrips();
    applyAction(tool_controller_.start(std::move(tool)));
    installToolOperator();
}

bool EditorSession::startTransformTool(TransformEditCommitMode commitMode) {
    if (!canStartTransformTool(commitMode)) {
        return false;
    }

    TransformEditContext context =
            TransformEditContext::fromSelection(*session_.document(), selection_service_.selected());
    if (!hasTransformableEntitySubject(context)) {
        return false;
    }

    startTool(std::make_unique<TransformTool>(session_.document(), std::move(context), TransformEditMode::Translate,
                                              commitMode));
    return true;
}

bool EditorSession::canStartTransformTool(TransformEditCommitMode commitMode) const {
    (void) commitMode;
    if (!isReady() || !session_.document() || selection_service_.empty()) {
        return false;
    }

    const TransformEditContext context =
            TransformEditContext::fromSelection(*session_.document(), selection_service_.selected());
    return hasTransformableEntitySubject(context);
}

bool EditorSession::startSelectionExtrudeTool() {
    if (!isReady() || !session_.document()) {
        return false;
    }
    startTool(std::make_unique<SelectionExtrudeTool>(*session_.document()));
    return true;
}

bool EditorSession::deleteSelectedEntities() {
    if (!isReady() || selection_service_.empty()) {
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
    const size_t deletedCount = entities.size();
    const bool changed = operation_executor_.execute(DocumentOperation::removeEntities(std::move(entities), true));
    if (changed) {
        selection_service_.clear();
        binding_.clearSelection();
        refreshGrips();
        LOG_DEBUG("[Editor] Deleted selected entities: count={}", deletedCount);
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
        selection_service_.pruneInvalid(*session_.document());
        refreshGrips();
        selection_service_.syncVisualState();
        LOG_DEBUG("[Editor] Undo completed");
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
        selection_service_.pruneInvalid(*session_.document());
        refreshGrips();
        selection_service_.syncVisualState();
        LOG_DEBUG("[Editor] Redo completed");
    }
    return changed;
}

bool EditorSession::handleInput(const engine::InputEvent& event) {
    // 有活动工具时，工具事件经栈上的 EditorToolOperator 处理（由 DocumentView 下行到 view 段）。
    // 此处不短路，返回 false 让事件继续到 view 段，使相机穿透（中键/右键/滚轮）生效。
    if (tool_controller_.hasActiveTool()) {
        return false;
    }

    // 无活动工具：尝试 grip 拖拽启动。
    return tryStartGripDrag(event);
}

EditorToolDispatchResult EditorSession::driveActiveTool(const engine::InputEvent& event) {
    EditorToolDispatchResult result;
    if (!tool_controller_.hasActiveTool()) {
        return result;
    }
    result.hadActiveTool = true;

    EditorInput input = makeEditorInput(event);
    updateSnapPreview(input);

    EditorAction action = tool_controller_.handleInput(input);
    result.lifecycle = action.lifecycle();
    result.consumed = applyAction(std::move(action));
    if (result.lifecycle != ToolLifecycle::Running) {
        overlay_service_.clear(EditorOverlayRole::Snap);
        refreshGrips();
    }
    return result;
}

void EditorSession::cancelActiveTool() {
    cancelToolState();
    removeToolOperator();
}

void EditorSession::cancelToolState() {
    applyAction(tool_controller_.cancel());
    overlay_service_.clearAll();
    refreshGrips();
}

void EditorSession::cancelActiveToolFromOperator() {
    cancelToolState();
}

void EditorSession::installToolOperator() {
    if (!tool_controller_.hasActiveTool()) {
        return;
    }

    // 防御性维护一工具一 Operator 不变量；正常路径在 startTool 前已移除旧对象。
    removeToolOperator();
    auto op = std::make_unique<EditorToolOperator>(*this);
    tool_operator_ = op.get();
    op->onFinish([this](engine::Operator& finished) {
        if (tool_operator_ == &finished) {
            tool_operator_ = nullptr;
        }
    });
    view_.pushOperator(std::move(op));
}

void EditorSession::removeToolOperator() {
    EditorToolOperator* installed = std::exchange(tool_operator_, nullptr);
    if (!installed) {
        return;
    }

    // 只撤销本 Session 安装的对象，不再清空所有非默认 Operator。
    if (!view_.removeOperator(installed)) {
        LOG_WARN("[Editor] Tool operator ownership was already released");
    }
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
    if (!isReady()) {
        return;
    }

    const std::optional<EditorSelectionHit> hit = pick_service_.selectionHitAtFramebuffer(screenX, screenY);
    if (hit) {
        selection_service_.selectSingleAndHover(*hit);
        binding_.selectSingle(hit->reference.entity);
    } else {
        selection_service_.clearSelectionAndHover();
        binding_.clearSelection();
    }
    refreshGrips();
}

void EditorSession::clearHover() {
    clearGripHover();
    selection_service_.clearHover();
}

void EditorSession::clearSnapPreview() {
    overlay_service_.clear(EditorOverlayRole::Snap);
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
    EditorInputResolveContext context;
    context.camera = &view_.camera();
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
    if (!event.isLeftPress()) {
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
    installToolOperator();
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
            selection_service_.pruneInvalid(*session_.document());
            refreshGrips();
            selection_service_.syncVisualState();
        }
    }

    return consumed;
}

}  // namespace mulan::editor
