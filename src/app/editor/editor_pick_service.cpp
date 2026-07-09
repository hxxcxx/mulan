#include "editor_pick_service.h"

#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/io/document.h>
#include <mulan/view/view_context.h>

namespace mulan::app {

namespace {

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

}  // namespace

void EditorPickService::bind(DocumentSession* session, view::ViewContext* view, DocumentViewBinding* binding) {
    session_ = session;
    view_ = view;
    binding_ = binding;
}

void EditorPickService::unbind() {
    session_ = nullptr;
    view_ = nullptr;
    binding_ = nullptr;
}

bool EditorPickService::isBound() const {
    return session_ != nullptr && view_ != nullptr && view_->isInitialized() && binding_ != nullptr;
}

bool EditorPickService::hasCursorPosition(const engine::InputEvent& event) {
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

EditorPickInput EditorPickService::inputPick(const engine::InputEvent& event) const {
    EditorPickInput input;
    if (!binding_) {
        return input;
    }

    input.renderScene = binding_->renderScene();
    if (!view_ || !hasCursorPosition(event)) {
        return input;
    }

    input.tested = true;
    if (const auto pick =
                binding_->pickAt(view_->camera(), static_cast<double>(event.x), static_cast<double>(event.y))) {
        input.hit = toEditorPickHit(*pick);
    }
    input.renderScene = binding_->renderScene();
    return input;
}

std::optional<EditorPickHit> EditorPickService::pickAtFramebuffer(double screenX, double screenY) const {
    if (!binding_ || !view_) {
        return std::nullopt;
    }

    const auto pick = binding_->pickAt(view_->camera(), screenX, screenY);
    if (!pick) {
        return std::nullopt;
    }

    EditorPickHit editorPick = toEditorPickHit(*pick);
    if (!editorPick.valid()) {
        return std::nullopt;
    }
    return editorPick;
}

std::optional<EditorSelectionHit> EditorPickService::selectionHitAtFramebuffer(double screenX, double screenY) const {
    if (!isBound() || !session_ || !session_->document()) {
        return std::nullopt;
    }

    const std::optional<EditorPickHit> pick = pickAtFramebuffer(screenX, screenY);
    if (!pick) {
        return std::nullopt;
    }
    return makeEditorSelectionHit(*pick, *session_->document());
}

const view::RenderScene* EditorPickService::renderScene() const {
    return binding_ ? binding_->renderScene() : nullptr;
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
        .curveCenter = pick.curveCenter,
        .curveNormal = pick.curveNormal,
        .curveRadius = pick.curveRadius,
        .hasCurveCircle = pick.hasCurveCircle,
        .curveStart = pick.curveStart,
        .curveEnd = pick.curveEnd,
        .curveMidpoint = pick.curveMidpoint,
        .hasCurveEndpoints = pick.hasCurveEndpoints,
        .hasCurveMidpoint = pick.hasCurveMidpoint,
        .curveClosed = pick.curveClosed,
        .curveStartDirection = pick.curveStartDirection,
        .curveSweepRadians = pick.curveSweepRadians,
        .hasCurveRange = pick.hasCurveRange,
        .barycentric = pick.barycentric,
        .hasBarycentric = pick.hasBarycentric,
    };
}

}  // namespace mulan::app
