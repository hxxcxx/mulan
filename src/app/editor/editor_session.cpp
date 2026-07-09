/**
 * @file editor_session.cpp
 * @brief EditorSession 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "editor_session.h"

#include "grip_drag_tool.h"
#include "grip_marker_builder.h"
#include "snap_marker_builder.h"
#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/math/math.h>
#include <mulan/view/view_context.h>

#include <cmath>
#include <string>
#include <utility>

namespace mulan::app {

namespace {

bool isLeftPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Left;
}

struct ScreenPoint {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
};

std::optional<ScreenPoint> projectToScreen(const engine::Camera& camera, const math::Point3& world) {
    const math::Vec4 clip = camera.viewProjectionMatrix() * math::Vec4(world.x, world.y, world.z, 1.0);
    if (std::abs(clip.w) <= 1.0e-12) {
        return std::nullopt;
    }

    const double ndcX = clip.x / clip.w;
    const double ndcY = clip.y / clip.w;
    const double ndcZ = clip.z / clip.w;
    if (ndcZ < -1.0 || ndcZ > 1.0) {
        return std::nullopt;
    }

    return ScreenPoint{
        .x = (ndcX + 1.0) * 0.5 * static_cast<double>(camera.width()),
        .y = (1.0 - ndcY) * 0.5 * static_cast<double>(camera.height()),
        .depth = ndcZ,
    };
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
    refreshGrips();
}

void EditorSession::unbind() {
    cancelActiveTool();
    clearGrips();
    selection_context_.clear();
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
    if (!isReady() || !session_ || !session_->document() || !view_ || tool_controller_.hasActiveTool()) {
        clearGrips();
        return;
    }

    grips_ = grip_provider_.build(*session_->document(), selection_context_);
    if (hovered_grip_ && !gripById(*hovered_grip_)) {
        hovered_grip_.reset();
    }
    rebuildGripPreview();
}

void EditorSession::clearGrips() {
    grips_.clear();
    hovered_grip_.reset();
    preview_controller_.clearGripGeometry();
    preview_controller_.clearGripHotGeometry();
}

bool EditorSession::updateGripHoverAtFramebuffer(double screenX, double screenY) {
    if (!isReady() || tool_controller_.hasActiveTool()) {
        clearGripHover();
        return false;
    }

    const std::optional<EditorGrip> grip = pickGripAt(screenX, screenY);
    const std::optional<EditorGripId> previous = hovered_grip_;
    hovered_grip_ = grip ? std::optional<EditorGripId>(grip->id) : std::nullopt;
    if (hovered_grip_ != previous) {
        rebuildGripPreview();
    }
    return grip.has_value();
}

void EditorSession::clearGripHover() {
    if (!hovered_grip_) {
        return;
    }
    hovered_grip_.reset();
    rebuildGripPreview();
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
            view_->setHoveredPickId(hit->reference.pickId);
        } else {
            view_->clearHoveredPickId();
        }
    }
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
            view_->setHoveredPickId(hit->reference.pickId);
        }
        binding_->selectSingle(hit->reference.entity);
    } else {
        selection_context_.clearSelection();
        selection_context_.clearHover();
        if (view_) {
            view_->clearHoveredPickId();
        }
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
}

void EditorSession::setSelectionFilter(EditorSelectionFilter filter) {
    selection_context_.setFilter(filter);
    selection_context_.clearHover();
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

void EditorSession::rebuildGripPreview() {
    if (!view_) {
        return;
    }

    const EditorGrip* hotGrip = hovered_grip_ ? gripById(*hovered_grip_) : nullptr;

    DraftGeometry gripGeometry = GripMarkerBuilder::build(
            grips_, view_->camera(), hotGrip ? std::optional<EditorGripId>(hotGrip->id) : std::nullopt);
    preview_controller_.setGripGeometry(std::move(gripGeometry));

    if (!hotGrip) {
        preview_controller_.clearGripHotGeometry();
        return;
    }

    DraftGeometry hotGeometry = GripMarkerBuilder::buildHot(*hotGrip, view_->camera());
    preview_controller_.setGripHotGeometry(std::move(hotGeometry));
}

bool EditorSession::tryStartGripDrag(const engine::InputEvent& event) {
    if (!isLeftPress(event)) {
        return false;
    }

    const std::optional<EditorGrip> grip = pickGripAt(static_cast<double>(event.x), static_cast<double>(event.y));
    if (!grip) {
        return false;
    }

    const math::Point3 dragStart = grip->worldPosition;
    clearGrips();
    preview_controller_.clearSnapGeometry();

    applyAction(tool_controller_.start(std::make_unique<GripDragTool>(*grip, dragStart)));
    return true;
}

std::optional<EditorGrip> EditorSession::pickGripAt(double screenX, double screenY) const {
    if (!view_) {
        return std::nullopt;
    }

    const engine::Camera& camera = view_->camera();
    std::optional<EditorGrip> best;
    double bestDistanceSq = 0.0;
    double bestDepth = 0.0;
    for (const EditorGrip& grip : grips_) {
        const auto screen = projectToScreen(camera, grip.worldPosition);
        if (!screen) {
            continue;
        }

        const double dx = screen->x - screenX;
        const double dy = screen->y - screenY;
        const double distanceSq = dx * dx + dy * dy;
        const double radiusSq = grip.pickRadiusPixels * grip.pickRadiusPixels;
        if (distanceSq > radiusSq) {
            continue;
        }

        if (!best || distanceSq < bestDistanceSq ||
            (std::abs(distanceSq - bestDistanceSq) <= 1.0e-6 && screen->depth < bestDepth)) {
            best = grip;
            bestDistanceSq = distanceSq;
            bestDepth = screen->depth;
        }
    }
    return best;
}

const EditorGrip* EditorSession::gripById(EditorGripId id) const {
    for (const EditorGrip& grip : grips_) {
        if (grip.id == id) {
            return &grip;
        }
    }
    return nullptr;
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
        operation_executor_.execute(std::move(*action.operation()));
    }

    return consumed;
}

}  // namespace mulan::app
