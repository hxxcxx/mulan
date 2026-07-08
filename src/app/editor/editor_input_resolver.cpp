#include "editor_input_resolver.h"

#include "editor_snap_collector.h"
#include "editor_snap_resolver.h"

namespace mulan::app {
namespace {

bool hasCursorPosition(engine::InputEvent::Type type) {
    switch (type) {
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

}  // namespace

EditorInput EditorInputResolver::resolve(const engine::InputEvent& event,
                                         const EditorInputResolveContext& context) const {
    EditorInput input;
    input.event = event;
    input.screenX = static_cast<double>(event.x);
    input.screenY = static_cast<double>(event.y);
    input.workPlane = work_plane_;
    input.axisAnchor = context.pointPolicy.axisAnchor;

    const engine::Camera* camera = context.camera;
    input.hasCursor = hasCursorPosition(event.type);
    if (!input.hasCursor || !camera || camera->width() <= 0 || camera->height() <= 0) {
        return input;
    }

    input.cursorRay = camera->screenRay(input.screenX, input.screenY);
    input.hasCursorRay = true;

    if (auto point = work_plane_.intersectScreen(*camera, input.screenX, input.screenY)) {
        input.workPoint = *point;
        input.workPlaneHit = true;
    }

    input.pickTested = context.pickTested;
    if (context.pickHit && context.pickHit->valid()) {
        input.pickHit = context.pickHit;
    }

    EditorSnapCollector::collect(
            EditorSnapCollectInput{
                    .event = event,
                    .workPlane = work_plane_,
                    .workPoint = input.workPoint,
                    .pickHit = input.pickHit,
                    .pointPolicy = context.pointPolicy,
                    .snapSettings = context.snapSettings,
            },
            input.snapCandidates);

    input.point = EditorSnapResolver::resolve(EditorSnapResolveInput{
            .candidates =
                    std::span<const EditorSnapCandidate>{ input.snapCandidates.data(), input.snapCandidates.size() },
            .pointPolicy = context.pointPolicy,
            .snapSettings = context.snapSettings,
    });
    if (input.point && input.point->geometry) {
        input.geometryDependency = input.point->geometry;
    }
    if (input.point && input.point->source == EditorPointSource::Snap) {
        input.snapResolved = true;
    }

    return input;
}

EditorInput EditorInputResolver::resolve(const engine::InputEvent& event, const engine::Camera& camera) const {
    EditorInputResolveContext context;
    context.camera = &camera;
    return resolve(event, context);
}

}  // namespace mulan::app
