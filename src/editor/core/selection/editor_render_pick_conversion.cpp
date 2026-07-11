#include "core/selection/editor_render_pick_conversion.h"

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

EditorPickHit editorPickHitFromRenderPick(const view::RenderScene::PickResult& pick) {
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
