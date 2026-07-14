#include "grip_drag_tool.h"

#include "../operation/control_polygon_builder.h"

#include <cmath>
#include <iterator>
#include <utility>
#include <variant>
#include <vector>

namespace mulan::editor {
namespace {

constexpr double kMinimumRadius = 1.0e-6;
constexpr double kMinimumSweepRadians = 1.0e-6;
constexpr double kControlPointMarkerPixels = 7.0;
constexpr double kFallbackControlPointMarkerRadius = 0.05;

bool isLeftPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Left;
}

bool isLeftRelease(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MouseRelease && event.button == engine::MouseButton::Left;
}

bool isRightPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Right;
}

bool isMouseMove(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MouseMove;
}

SelectionTarget selectionTargetForGrip(const EditorGrip& grip) {
    SelectionTarget target;
    target.entity = grip.entity;
    target.domain = EditorSelectionDomain::Curve;
    target.kind = EditorSubEntityKind::Grip;
    target.subObject.curveElement = grip.element;
    target.subObject.curveKind = grip.primitiveKind;
    target.subObject.hasComponentIndex = true;
    switch (grip.kind) {
    case EditorGripKind::Vertex:
    case EditorGripKind::ControlPoint:
    case EditorGripKind::Radius: target.subObject.componentIndex = grip.vertexIndex; break;
    case EditorGripKind::Midpoint: target.subObject.componentIndex = grip.segmentIndex; break;
    case EditorGripKind::Center: target.subObject.componentIndex = 0; break;
    }
    return target;
}

DragEditDescriptor dragDescriptorForGrip(const EditorGrip& grip, const math::Point3& dragStartWorld) {
    return DragEditDescriptor{
        .target = selectionTargetForGrip(grip),
        .subjectKind = DragEditSubjectKind::Grip,
        .startWorld = dragStartWorld,
        .localToWorld = grip.localToWorld,
        .worldToLocal = grip.worldToLocal,
    };
}

asset::CurvePrimitive translatePrimitive(const asset::CurvePrimitive& primitive, const math::Vec3& delta) {
    const auto& data = primitive.data();
    if (const auto* segment = std::get_if<asset::CurveSegmentPrimitive>(&data)) {
        return asset::CurvePrimitive::segment(
                math::Segment3(segment->segment.start + delta, segment->segment.end + delta));
    }
    if (const auto* polyline = std::get_if<asset::CurvePolylinePrimitive>(&data)) {
        math::Polyline3 moved = polyline->polyline;
        for (math::Point3& point : moved.points) {
            point += delta;
        }
        return asset::CurvePrimitive::polyline(moved);
    }
    if (const auto* circle = std::get_if<asset::CurveCirclePrimitive>(&data)) {
        math::Circle3 moved = circle->circle;
        moved.center += delta;
        return asset::CurvePrimitive::circle(moved);
    }
    if (const auto* arc = std::get_if<asset::CurveArcPrimitive>(&data)) {
        math::Arc3 moved = arc->arc;
        moved.center += delta;
        return asset::CurvePrimitive::arc(moved);
    }
    if (const auto* bezier = std::get_if<asset::CurveBezierPrimitive>(&data)) {
        std::vector<math::Point3> points = bezier->curve.controlPoints();
        for (math::Point3& point : points) {
            point += delta;
        }
        return asset::CurvePrimitive::bezier(math::BezierCurve3d(std::move(points)));
    }
    if (const auto* bspline = std::get_if<asset::CurveBSplinePrimitive>(&data)) {
        std::vector<math::Point3> points = bspline->curve.controlPoints();
        for (math::Point3& point : points) {
            point += delta;
        }
        return asset::CurvePrimitive::bspline(
                math::BSplineCurve3d(bspline->curve.degree(), std::move(points), bspline->curve.knots()));
    }
    if (const auto* nurbs = std::get_if<asset::CurveNurbsPrimitive>(&data)) {
        std::vector<math::Point3> points = nurbs->curve.controlPoints();
        for (math::Point3& point : points) {
            point += delta;
        }
        return asset::CurvePrimitive::nurbs(math::NURBSCurve3d(nurbs->curve.degree(), std::move(points),
                                                               nurbs->curve.weights(), nurbs->curve.knots()));
    }
    return primitive;
}

double signedAngleAround(const math::Vec3& from, const math::Vec3& to, const math::Vec3& normal) {
    const math::Vec3 n = normal.normalizedOr(math::Vec3::unitZ());
    const math::Vec3 a = (from - n * from.dot(n)).normalizedOr(math::Vec3::unitX());
    const math::Vec3 b = (to - n * to.dot(n)).normalizedOr(a);
    return std::atan2(n.dot(a.cross(b)), a.dot(b));
}

std::optional<math::Vec3> directionFromCenterOnPlane(const math::Point3& center, const math::Point3& target,
                                                     const math::Vec3& normal) {
    const math::Vec3 n = normal.normalizedOr(math::Vec3::unitZ());
    math::Vec3 direction = target - center;
    direction -= n * direction.dot(n);
    if (direction.lengthSq() <= kMinimumRadius * kMinimumRadius) {
        return std::nullopt;
    }
    return direction.normalized();
}

math::Angle orientedSweep(const math::Vec3& from, const math::Vec3& to, const math::Vec3& normal,
                          math::Angle referenceSweep) {
    double sweep = signedAngleAround(from, to, normal);
    const double fullTurn = math::Angle::fullTurn().radians();
    if (referenceSweep.radians() >= 0.0) {
        if (sweep < 0.0) {
            sweep += fullTurn;
        }
    } else if (sweep > 0.0) {
        sweep -= fullTurn;
    }
    return math::Angle::fromRad(sweep);
}

std::optional<asset::CurvePrimitive> moveArcEndpoint(const EditorGrip& grip, const math::Point3& targetLocal) {
    const auto* arcPrimitive = std::get_if<asset::CurveArcPrimitive>(&grip.sourcePrimitive.data());
    if (!arcPrimitive) {
        return std::nullopt;
    }

    const math::Arc3& arc = arcPrimitive->arc;
    if (!arc.valid() || arc.sweep == math::Angle::zero()) {
        return std::nullopt;
    }

    const std::optional<math::Vec3> targetDirection = directionFromCenterOnPlane(arc.center, targetLocal, arc.normal);
    if (!targetDirection) {
        return std::nullopt;
    }

    math::Arc3 edited = arc;
    if (grip.vertexIndex == 0) {
        const math::Vec3 oldEndDirection = math::rotateAroundAxis(arc.startDirection, arc.normal, arc.sweep);
        edited.startDirection = *targetDirection;
        edited.sweep = orientedSweep(edited.startDirection, oldEndDirection, arc.normal, arc.sweep);
    } else if (grip.vertexIndex == 1) {
        edited.sweep = orientedSweep(arc.startDirection, *targetDirection, arc.normal, arc.sweep);
    } else {
        return std::nullopt;
    }

    if (std::abs(edited.sweep.radians()) <= kMinimumSweepRadians) {
        return std::nullopt;
    }
    return asset::CurvePrimitive::arc(edited);
}

std::optional<asset::CurvePrimitive> moveVertex(const EditorGrip& grip, const math::Point3& targetLocal) {
    const auto& data = grip.sourcePrimitive.data();
    if (const auto* segment = std::get_if<asset::CurveSegmentPrimitive>(&data)) {
        math::Segment3 edited = segment->segment;
        if (grip.vertexIndex == 0) {
            edited.start = targetLocal;
        } else if (grip.vertexIndex == 1) {
            edited.end = targetLocal;
        } else {
            return std::nullopt;
        }
        if (edited.lengthSq() <= 1.0e-12) {
            return std::nullopt;
        }
        return asset::CurvePrimitive::segment(edited);
    }

    if (const auto* polyline = std::get_if<asset::CurvePolylinePrimitive>(&data)) {
        math::Polyline3 edited = polyline->polyline;
        if (grip.vertexIndex >= edited.points.size()) {
            return std::nullopt;
        }
        edited.points[grip.vertexIndex] = targetLocal;
        if (!edited.hasSegments()) {
            return std::nullopt;
        }
        return asset::CurvePrimitive::polyline(edited);
    }

    if (std::get_if<asset::CurveArcPrimitive>(&data)) {
        return moveArcEndpoint(grip, targetLocal);
    }

    return std::nullopt;
}

std::optional<asset::CurvePrimitive> moveControlPoint(const EditorGrip& grip, const math::Point3& targetLocal) {
    const auto& data = grip.sourcePrimitive.data();
    if (const auto* bezier = std::get_if<asset::CurveBezierPrimitive>(&data)) {
        std::vector<math::Point3> points = bezier->curve.controlPoints();
        if (grip.vertexIndex >= points.size()) {
            return std::nullopt;
        }
        points[grip.vertexIndex] = targetLocal;
        return asset::CurvePrimitive::bezier(math::BezierCurve3d(std::move(points)));
    }

    if (const auto* bspline = std::get_if<asset::CurveBSplinePrimitive>(&data)) {
        std::vector<math::Point3> points = bspline->curve.controlPoints();
        if (grip.vertexIndex >= points.size()) {
            return std::nullopt;
        }
        points[grip.vertexIndex] = targetLocal;
        return asset::CurvePrimitive::bspline(
                math::BSplineCurve3d(bspline->curve.degree(), std::move(points), bspline->curve.knots()));
    }

    if (const auto* nurbs = std::get_if<asset::CurveNurbsPrimitive>(&data)) {
        std::vector<math::Point3> points = nurbs->curve.controlPoints();
        if (grip.vertexIndex >= points.size()) {
            return std::nullopt;
        }
        points[grip.vertexIndex] = targetLocal;
        return asset::CurvePrimitive::nurbs(math::NURBSCurve3d(nurbs->curve.degree(), std::move(points),
                                                               nurbs->curve.weights(), nurbs->curve.knots()));
    }

    return moveVertex(grip, targetLocal);
}

std::optional<asset::CurvePrimitive> movePolylineSegment(const EditorGrip& grip, const math::Vec3& deltaLocal) {
    const auto* polyline = std::get_if<asset::CurvePolylinePrimitive>(&grip.sourcePrimitive.data());
    if (!polyline) {
        return translatePrimitive(grip.sourcePrimitive, deltaLocal);
    }

    math::Polyline3 edited = polyline->polyline;
    if (!edited.hasSegments() || grip.segmentIndex >= edited.segmentCount()) {
        return std::nullopt;
    }

    const size_t first = grip.segmentIndex;
    const size_t second = (grip.segmentIndex + 1) % edited.points.size();
    if (!edited.closed && second <= first) {
        return std::nullopt;
    }

    edited.points[first] += deltaLocal;
    edited.points[second] += deltaLocal;
    return asset::CurvePrimitive::polyline(edited);
}

std::optional<double> radiusFromTarget(const math::Point3& center, const math::Point3& target,
                                       const math::Vec3& normal) {
    const math::Vec3 n = normal.normalizedOr(math::Vec3::unitZ());
    math::Vec3 radiusVector = target - center;
    radiusVector -= n * radiusVector.dot(n);
    const double radius = radiusVector.length();
    if (radius <= kMinimumRadius) {
        return std::nullopt;
    }
    return radius;
}

std::optional<asset::CurvePrimitive> changeRadius(const EditorGrip& grip, const math::Point3& targetLocal) {
    const auto& data = grip.sourcePrimitive.data();
    if (const auto* circle = std::get_if<asset::CurveCirclePrimitive>(&data)) {
        math::Circle3 edited = circle->circle;
        const std::optional<double> radius = radiusFromTarget(edited.center, targetLocal, edited.normal);
        if (!radius) {
            return std::nullopt;
        }

        edited.radius = *radius;
        return asset::CurvePrimitive::circle(edited);
    }

    if (const auto* arc = std::get_if<asset::CurveArcPrimitive>(&data)) {
        math::Arc3 edited = arc->arc;
        const std::optional<double> radius = radiusFromTarget(edited.center, targetLocal, edited.normal);
        if (!radius) {
            return std::nullopt;
        }

        edited.radius = *radius;
        return asset::CurvePrimitive::arc(edited);
    }

    return std::nullopt;
}

double transformedRadiusScale(const math::Vec3& localDirection, const math::Mat4& transform) {
    const double scale = localDirection.transformedAsDir(transform).length();
    return scale > 1.0e-9 ? scale : 1.0;
}

asset::CurvePrimitive transformPrimitiveToWorld(const asset::CurvePrimitive& primitive, const math::Mat4& transform) {
    const auto& data = primitive.data();
    if (const auto* segment = std::get_if<asset::CurveSegmentPrimitive>(&data)) {
        return asset::CurvePrimitive::segment(segment->segment.transformed(transform));
    }
    if (const auto* polyline = std::get_if<asset::CurvePolylinePrimitive>(&data)) {
        return asset::CurvePrimitive::polyline(polyline->polyline.transformed(transform));
    }
    if (const auto* circle = std::get_if<asset::CurveCirclePrimitive>(&data)) {
        const math::Circle3& source = circle->circle;
        const math::Vec3 normal = source.normal.transformedAsNormal(transform).normalizedOr(math::Vec3::unitZ());
        const math::Vec3 radiusAxis = math::perpendicularUnit(source.normal);
        return asset::CurvePrimitive::circle(
                math::Circle3(source.center.transformedBy(transform),
                              source.radius * transformedRadiusScale(radiusAxis, transform), normal));
    }
    if (const auto* arc = std::get_if<asset::CurveArcPrimitive>(&data)) {
        const math::Arc3& source = arc->arc;
        const math::Vec3 normal = source.normal.transformedAsNormal(transform).normalizedOr(math::Vec3::unitZ());
        const math::Vec3 startDirection =
                source.startDirection.transformedAsDir(transform).normalizedOr(math::Vec3::unitX());
        return asset::CurvePrimitive::arc(
                math::Arc3(source.center.transformedBy(transform),
                           source.radius * transformedRadiusScale(source.startDirection, transform), startDirection,
                           source.sweep, normal));
    }
    if (const auto* bezier = std::get_if<asset::CurveBezierPrimitive>(&data)) {
        std::vector<math::Point3> points = bezier->curve.controlPoints();
        for (math::Point3& point : points) {
            point = point.transformedBy(transform);
        }
        return asset::CurvePrimitive::bezier(math::BezierCurve3d(std::move(points)));
    }
    if (const auto* bspline = std::get_if<asset::CurveBSplinePrimitive>(&data)) {
        std::vector<math::Point3> points = bspline->curve.controlPoints();
        for (math::Point3& point : points) {
            point = point.transformedBy(transform);
        }
        return asset::CurvePrimitive::bspline(
                math::BSplineCurve3d(bspline->curve.degree(), std::move(points), bspline->curve.knots()));
    }
    if (const auto* nurbs = std::get_if<asset::CurveNurbsPrimitive>(&data)) {
        std::vector<math::Point3> points = nurbs->curve.controlPoints();
        for (math::Point3& point : points) {
            point = point.transformedBy(transform);
        }
        return asset::CurvePrimitive::nurbs(math::NURBSCurve3d(nurbs->curve.degree(), std::move(points),
                                                               nurbs->curve.weights(), nurbs->curve.knots()));
    }
    return primitive;
}

std::vector<math::Point3> controlPointsOf(const asset::CurvePrimitive& primitive) {
    const auto& data = primitive.data();
    if (const auto* bezier = std::get_if<asset::CurveBezierPrimitive>(&data)) {
        return bezier->curve.controlPoints();
    }
    if (const auto* bspline = std::get_if<asset::CurveBSplinePrimitive>(&data)) {
        return bspline->curve.controlPoints();
    }
    if (const auto* nurbs = std::get_if<asset::CurveNurbsPrimitive>(&data)) {
        return nurbs->curve.controlPoints();
    }
    return {};
}

}  // namespace

GripDragTool::GripDragTool(EditorGrip grip, math::Point3 dragStartWorld)
    : grip_(std::move(grip)), drag_(dragDescriptorForGrip(grip_, dragStartWorld)) {
}

EditorPointPolicy GripDragTool::pointPolicy() const {
    EditorPointPolicy policy;
    policy.axisAnchor = grip_.worldPosition;
    return policy;
}

EditorAction GripDragTool::begin() {
    current_primitive_.reset();
    return EditorAction::clearPreview();
}

EditorAction GripDragTool::handleInput(const EditorInput& input) {
    if (isRightPress(input.event)) {
        return EditorAction::cancel();
    }

    if (isLeftPress(input.event)) {
        return EditorAction::consumeEvent();
    }

    // 生命周期事件优先于空间数据（修 P7）：release 必须能结束工具，即使 worldPoint 缺失。
    if (isLeftRelease(input.event)) {
        const auto point = input.worldPoint();
        if (point) {
            return commitAt(*point);
        }
        // worldPoint 缺失时用最后一次预览的图元回退提交，确保 release 总能完成。
        if (current_primitive_) {
            return commitPrimitive(*current_primitive_);
        }
        return EditorAction::cancel();  // 无可提交：取消而非吞事件
    }

    const auto point = input.worldPoint();
    if (!point) {
        return EditorAction::consumeEvent();
    }

    if (isMouseMove(input.event)) {
        return updatePreview(input, *point);
    }

    return EditorAction::ignored();
}

EditorAction GripDragTool::end(ToolFinishReason reason) {
    drag_.clearPreview();
    current_primitive_.reset();
    if (reason != ToolFinishReason::Finished) {
        return EditorAction::clearPreview();
    }
    return EditorAction::ignored();
}

EditorAction GripDragTool::updatePreview(const EditorInput& input, const math::Point3& worldPoint) {
    const DragEditSample sample = drag_.update(worldPoint);
    current_primitive_ = makeEditedPrimitive(sample);
    if (!current_primitive_) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(previewGeometry(input, *current_primitive_));
}

EditorAction GripDragTool::commitAt(const math::Point3& worldPoint) {
    const DragEditSample sample = drag_.update(worldPoint);
    std::optional<asset::CurvePrimitive> primitive = makeEditedPrimitive(sample);
    if (!primitive) {
        primitive = current_primitive_;
    }
    if (!primitive) {
        return EditorAction::cancel();
    }
    return commitPrimitive(*primitive);
}

EditorAction GripDragTool::commitPrimitive(const asset::CurvePrimitive& primitive) {
    EditorAction action =
            EditorAction::commit(DocumentOperation::updateCurve(grip_.entity, grip_.element, primitive));
    action.clearPreviewOnApply().finishTool();
    return action;
}

std::optional<asset::CurvePrimitive> GripDragTool::makeEditedPrimitive(const DragEditSample& sample) const {
    switch (grip_.action) {
    case EditorGripAction::MovePrimitive: return translatePrimitive(grip_.sourcePrimitive, sample.deltaLocal);
    case EditorGripAction::MoveSegment: return movePolylineSegment(grip_, sample.deltaLocal);
    case EditorGripAction::MoveVertex: return moveVertex(grip_, sample.currentLocal);
    case EditorGripAction::MoveControlPoint: return moveControlPoint(grip_, sample.currentLocal);
    case EditorGripAction::ChangeRadius: return changeRadius(grip_, sample.currentLocal);
    }
    return std::nullopt;
}

DraftGeometry GripDragTool::previewGeometry(const EditorInput& input, const asset::CurvePrimitive& primitive) const {
    asset::CurvePrimitive worldPrimitive = transformPrimitiveToWorld(primitive, grip_.localToWorld);
    std::vector<asset::CurvePrimitive> curves;
    curves.push_back(worldPrimitive);

    std::vector<math::Point3> controlPoints = controlPointsOf(worldPrimitive);
    if (controlPoints.empty()) {
        return DraftGeometry::curves(std::move(curves));
    }

    const ControlMarkerBasis basis = input.snapQuery.camera ? controlMarkerBasisFromCamera(*input.snapQuery.camera)
                                                            : controlMarkerBasisFromNormal(math::Vec3::unitZ());
    DraftGeometry controlGeometry =
            input.snapQuery.camera
                    ? buildControlPolygonGeometry(controlPoints, basis, *input.snapQuery.camera,
                                                  kControlPointMarkerPixels)
                    : buildControlPolygonGeometry(controlPoints, basis, kFallbackControlPointMarkerRadius);
    std::vector<asset::CurvePrimitive> controlCurves = controlGeometry.takeCurves();
    std::vector<graphics::Mesh> controlMeshes = controlGeometry.takeMeshes();
    curves.insert(curves.end(), std::make_move_iterator(controlCurves.begin()),
                  std::make_move_iterator(controlCurves.end()));
    return DraftGeometry::geometry(std::move(curves), std::move(controlMeshes));
}

}  // namespace mulan::editor
