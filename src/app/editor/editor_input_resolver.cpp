#include "editor_input_resolver.h"

#include "editor_snap_resolver.h"

namespace mulan::app {
namespace {

EditorGeometryDependency geometryDependencyFromHit(const EditorPickHit& hit) {
    return EditorGeometryDependency{
        .entity = hit.entity,
        .pickId = hit.pickId,
        .hitKind = hit.kind,
        .distance = hit.distance,
        .sourceDrawableIndex = hit.sourceDrawableIndex,
        .primitiveIndex = hit.primitiveIndex,
        .hasPrimitiveIndex = hit.hasPrimitiveIndex,
        .parameter = hit.parameter,
    };
}

EditorSnapKind snapKindForHit(EditorPickHitKind kind) {
    switch (kind) {
    case EditorPickHitKind::Vertex: return EditorSnapKind::Vertex;
    case EditorPickHitKind::Edge: return EditorSnapKind::Edge;
    case EditorPickHitKind::Face: return EditorSnapKind::Face;
    case EditorPickHitKind::Curve: return EditorSnapKind::Curve;
    case EditorPickHitKind::Object:
    case EditorPickHitKind::None: return EditorSnapKind::None;
    }
    return EditorSnapKind::None;
}

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
        input.snapCandidates.push_back(EditorSnapCandidate{
                .world = *point,
                .kind = EditorSnapKind::WorkPlane,
                .dependency = EditorPointDependencyKind::WorkPlane,
                .priority = 0.0,
        });
    }

    input.pickTested = context.pickTested;
    if (context.pickHit && context.pickHit->valid()) {
        input.pickHit = context.pickHit;
        if (context.pickHit->hasWorldPoint) {
            const EditorGeometryDependency dependency = geometryDependencyFromHit(*context.pickHit);
            const EditorSnapKind kind = snapKindForHit(context.pickHit->kind);
            input.snapCandidates.push_back(EditorSnapCandidate{
                    .world = context.pickHit->worldPoint,
                    .kind = kind != EditorSnapKind::None ? kind : EditorSnapKind::Face,
                    .dependency = EditorPointDependencyKind::Geometry,
                    .geometry = dependency,
                    .priority = 1.0,
            });
        }
    }

    input.point = EditorSnapResolver::resolve(EditorSnapResolveInput{
            .candidates =
                    std::span<const EditorSnapCandidate>{ input.snapCandidates.data(), input.snapCandidates.size() },
            .workPoint = input.workPoint,
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
