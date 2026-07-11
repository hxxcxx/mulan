#include "core/snap/editor_input_resolver.h"

#include "core/snap/editor_snap_collector.h"
#include "core/snap/editor_snap_resolver.h"

#include <mulan/render/camera/camera.h>

#include <algorithm>
#include <cmath>

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

double snapTolerancePixels(const EditorSnapSettings& settings) {
    return std::max(0.0, settings.snapTolerancePixels);
}

double screenPixelsToWorldTolerance(const engine::Camera& camera, double pixels) {
    const double viewportHeight = static_cast<double>(std::max(1, camera.height()));
    if (camera.isOrthographic()) {
        return pixels * (2.0 * camera.orthoSize()) / viewportHeight;
    }

    const double viewHeightAtTarget =
            2.0 * std::max(camera.distance(), camera.nearPlane()) * std::tan(camera.fieldOfView() * 0.5);
    return pixels * viewHeightAtTarget / viewportHeight;
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

    input.snapQuery = EditorSnapQuery{
        .event = event,
        .camera = camera,
        .pickWorld = context.pickWorld,
        .workPlane = work_plane_,
        .cursorRay = input.cursorRay,
        .screenX = input.screenX,
        .screenY = input.screenY,
        .workPoint = input.workPoint,
        .primaryPickHit = input.pickHit,
        .pointPolicy = context.pointPolicy,
        .snapSettings = context.snapSettings,
        .tolerancePixels = snapTolerancePixels(context.snapSettings),
        .toleranceWorld = context.snapSettings.snapToleranceWorld > 0.0
                                  ? context.snapSettings.snapToleranceWorld
                                  : screenPixelsToWorldTolerance(*camera, snapTolerancePixels(context.snapSettings)),
        .hasCursor = input.hasCursor,
        .hasCursorRay = input.hasCursorRay,
        .workPlaneHit = input.workPlaneHit,
    };

    EditorSnapCollector::collect(
            EditorSnapCollectInput{
                    .query = input.snapQuery,
            },
            input.snapCandidates);

    input.snapResult = EditorSnapResolver::resolveResult(EditorSnapResolveInput{
            .candidates =
                    std::span<const EditorSnapCandidate>{ input.snapCandidates.data(), input.snapCandidates.size() },
            .pointPolicy = context.pointPolicy,
            .snapSettings = context.snapSettings,
    });
    input.point = input.snapResult.point;
    if (input.point && input.point->geometry) {
        input.geometryDependency = input.point->geometry;
    }
    input.snapResolved = input.snapResult.resolved;

    return input;
}

EditorInput EditorInputResolver::resolve(const engine::InputEvent& event, const engine::Camera& camera) const {
    EditorInputResolveContext context;
    context.camera = &camera;
    return resolve(event, context);
}

}  // namespace mulan::app
