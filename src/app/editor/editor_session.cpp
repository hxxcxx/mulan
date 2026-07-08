/**
 * @file editor_session.cpp
 * @brief EditorSession 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "editor_session.h"

#include "snap_marker_builder.h"
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
    if (view_) {
        view_->previewLayer().clearSnapGeometry();
    }
    applyAction(tool_controller_.start(std::move(tool)));
}

bool EditorSession::handleInput(const engine::InputEvent& event) {
    if (!tool_controller_.hasActiveTool() || !view_) {
        return false;
    }

    EditorInput input = makeEditorInput(event);
    updateSnapPreview(input);

    EditorAction action = tool_controller_.handleInput(input);
    const ToolLifecycle lifecycle = action.lifecycle();
    const bool consumed = applyAction(std::move(action));
    if (lifecycle != ToolLifecycle::Running && view_) {
        view_->previewLayer().clearSnapGeometry();
    }
    return consumed;
}

void EditorSession::cancelActiveTool() {
    applyAction(tool_controller_.cancel());
    if (view_) {
        view_->clearPreview();
    }
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
               },
               operation.data());

    if (changed && binding_) {
        binding_->refresh();
    }
    return changed;
}

}  // namespace mulan::app
