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

#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>
#include <mulan/math/math.h>
#include <mulan/view/preview_layer.h>
#include <mulan/view/view_context.h>

#include <cmath>
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

bool hasCursorPosition(const engine::InputEvent& event) {
    switch (event.type) {
    case engine::InputEvent::Type::MousePress:
    case engine::InputEvent::Type::MouseRelease:
    case engine::InputEvent::Type::MouseMove:
    case engine::InputEvent::Type::MouseDoubleClick:
    case engine::InputEvent::Type::Wheel: return true;
    case engine::InputEvent::Type::KeyPress:
    case engine::InputEvent::Type::KeyRelease: return false;
    }
    return false;
}

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

EditorPickHitKind toEditorPickHitKind(view::RenderScene::PickHitKind kind) {
    switch (kind) {
    case view::RenderScene::PickHitKind::Object: return EditorPickHitKind::Object;
    case view::RenderScene::PickHitKind::Vertex: return EditorPickHitKind::Vertex;
    case view::RenderScene::PickHitKind::Edge: return EditorPickHitKind::Edge;
    case view::RenderScene::PickHitKind::Face: return EditorPickHitKind::Face;
    case view::RenderScene::PickHitKind::Curve: return EditorPickHitKind::Curve;
    case view::RenderScene::PickHitKind::None: return EditorPickHitKind::None;
    }
    return EditorPickHitKind::None;
}

EditorPickHit toEditorPickHit(const view::RenderScene::PickResult& pick) {
    return EditorPickHit{
        .entity = pick.entity,
        .pickId = pick.pickId,
        .kind = toEditorPickHitKind(pick.kind),
        .distance = pick.distance,
        .worldPoint = pick.worldPoint,
        .hasWorldPoint = pick.hasWorldPoint,
        .worldNormal = pick.worldNormal,
        .hasWorldNormal = pick.hasWorldNormal,
        .sourceDrawableIndex = pick.sourceDrawableIndex,
        .primitiveIndex = pick.primitiveIndex,
        .hasPrimitiveIndex = pick.hasPrimitiveIndex,
        .parameter = pick.parameter,
        .toleranceWorld = pick.toleranceWorld,
        .edgeStart = pick.edgeStart,
        .edgeEnd = pick.edgeEnd,
        .hasEdgeSegment = pick.hasEdgeSegment,
        .barycentric = pick.barycentric,
        .hasBarycentric = pick.hasBarycentric,
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
    refreshGrips();
}

void EditorSession::unbind() {
    cancelActiveTool();
    clearGrips();
    session_ = nullptr;
    view_ = nullptr;
    binding_ = nullptr;
}

bool EditorSession::isReady() const {
    return session_ != nullptr && view_ != nullptr && view_->isInitialized() && binding_ != nullptr;
}

void EditorSession::startTool(std::unique_ptr<EditorTool> tool) {
    if (view_) {
        view_->previewLayer().clearSnapGeometry();
    }
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
        view_->previewLayer().clearSnapGeometry();
        refreshGrips();
    }
    return consumed;
}

void EditorSession::cancelActiveTool() {
    applyAction(tool_controller_.cancel());
    if (view_) {
        view_->clearPreview();
    }
    refreshGrips();
}

void EditorSession::refreshGrips() {
    if (!isReady() || !session_ || !session_->document() || !view_ || tool_controller_.hasActiveTool()) {
        clearGrips();
        return;
    }

    grips_ = grip_provider_.build(*session_->document());
    if (hovered_grip_ && !gripById(*hovered_grip_)) {
        hovered_grip_.reset();
    }
    rebuildGripPreview();
}

void EditorSession::clearGrips() {
    grips_.clear();
    hovered_grip_.reset();
    if (view_) {
        view_->previewLayer().clearGripGeometry();
        view_->previewLayer().clearGripHotGeometry();
    }
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

    if (binding_ && hasCursorPosition(event)) {
        context.pickTested = true;
        if (const auto pick = binding_->pickEntityAt(view_->camera(), static_cast<double>(event.x),
                                                     static_cast<double>(event.y))) {
            context.pickHit = toEditorPickHit(*pick);
        }
        context.renderScene = binding_->renderScene();
    }

    return input_resolver_.resolve(event, context);
}

void EditorSession::updateSnapPreview(const EditorInput& input) {
    if (!view_) {
        return;
    }

    DraftGeometry marker = SnapMarkerBuilder::build(input);
    if (marker.empty()) {
        view_->previewLayer().clearSnapGeometry();
        return;
    }

    view_->previewLayer().setSnapGeometry(marker.takeCurves(), marker.takeMeshes());
}

void EditorSession::rebuildGripPreview() {
    if (!view_) {
        return;
    }

    const EditorGrip* hotGrip = hovered_grip_ ? gripById(*hovered_grip_) : nullptr;

    DraftGeometry gripGeometry = GripMarkerBuilder::build(
            grips_, view_->camera(), hotGrip ? std::optional<EditorGripId>(hotGrip->id) : std::nullopt);
    if (gripGeometry.empty()) {
        view_->previewLayer().clearGripGeometry();
    } else {
        view_->previewLayer().setGripGeometry(gripGeometry.takeCurves(), gripGeometry.takeMeshes());
    }

    if (!hotGrip) {
        view_->previewLayer().clearGripHotGeometry();
        return;
    }

    DraftGeometry hotGeometry = GripMarkerBuilder::buildHot(*hotGrip, view_->camera());
    if (hotGeometry.empty()) {
        view_->previewLayer().clearGripHotGeometry();
    } else {
        view_->previewLayer().setGripHotGeometry(hotGeometry.takeCurves(), hotGeometry.takeMeshes());
    }
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
    if (view_) {
        view_->previewLayer().clearSnapGeometry();
    }

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

    if (action.shouldClearPreview() && view_) {
        view_->previewLayer().clearToolGeometry();
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
                       [&editor, &changed](UpdateCurveOperation& update) {
                           changed = editor.updateCurve(update.entity, update.element, std::move(update.primitive));
                       },
               },
               operation.data());

    if (changed && binding_) {
        binding_->refresh();
    }
    return changed;
}

}  // namespace mulan::app
