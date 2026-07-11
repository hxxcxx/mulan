#include "core/tools/extrude_tool.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace mulan::app {
namespace {

constexpr double kMinimumEdgeLengthSq = 1.0e-12;
constexpr double kMinimumExtrudeDistance = 1.0e-9;
constexpr double kCloseToStartPixels = 10.0;
constexpr double kPixelsPerProfileExtent = 180.0;

asset::FacePlaneFrame makeFrame(const ToolPoint& point) {
    const math::Vec3 normal = point.workPlane.plane().normal.normalizedOr(math::Vec3::unitZ());
    const math::Plane3 plane = math::Plane3::fromPointNormal(point.world(), normal);
    const math::Vec3 seed = std::abs(normal.z) < 0.9 ? math::Vec3::unitZ() : math::Vec3::unitY();
    const math::Vec3 x = seed.cross(normal).normalizedOr(math::Vec3::unitX());
    const math::Vec3 y = normal.cross(x).normalizedOr(math::Vec3::unitY());
    return asset::FacePlaneFrame{
        .origin = plane.project(point.world()),
        .x = x,
        .y = y,
        .normal = normal,
    };
}

asset::CurvePrimitive closedLoopCurve(const std::vector<math::Point3>& points) {
    return asset::CurvePrimitive::polyline(math::Polyline3(points, true));
}

}  // namespace

EditorAction ExtrudeTool::onPointPressed(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    if (phase_ == Phase::Extruding) {
        return EditorAction::consumeEvent();
    }

    ToolPoint normalized = normalizePointToFrame(point);
    if (closesToFirst(normalized)) {
        return finishProfile(input);
    }
    return acceptProfilePoint(std::move(normalized));
}

EditorAction ExtrudeTool::onPointMoved(const EditorInput& input, const ToolPoint& point) {
    if (phase_ == Phase::Extruding) {
        signed_distance_ = signedDistanceFor(input);
        return updateExtrudePreview(signed_distance_);
    }

    if (!hasAcceptedPoints()) {
        return EditorAction::consumeEvent();
    }
    ToolPoint normalized = normalizePointToFrame(point);
    return updateProfilePreview(&normalized);
}

EditorAction ExtrudeTool::onRightPressed(const EditorInput& input) {
    if (phase_ == Phase::DrawingProfile) {
        return finishProfile(input);
    }
    return finishExtrude();
}

void ExtrudeTool::resetDrawingState() {
    phase_ = Phase::DrawingProfile;
    has_frame_ = false;
    frame_ = asset::FacePlaneFrame{};
    profile_ = asset::FaceDefinition{};
    extrusion_anchor_y_ = 0.0;
    signed_distance_ = 0.0;
}

EditorAction ExtrudeTool::acceptProfilePoint(ToolPoint point) {
    const ToolPoint* last = lastAcceptedPoint();
    if (last && last->world().distanceSq(point.world()) <= kMinimumEdgeLengthSq) {
        return updateProfilePreview(nullptr);
    }

    addAcceptedPoint(std::move(point));
    return updateProfilePreview(nullptr);
}

EditorAction ExtrudeTool::finishProfile(const EditorInput& input) {
    const std::optional<asset::FaceDefinition> profile = completedProfile();
    if (!profile) {
        return EditorAction::consumeEvent();
    }

    profile_ = *profile;
    phase_ = Phase::Extruding;
    extrusion_anchor_y_ = input.screenY;
    signed_distance_ = 0.0;
    return updateExtrudePreview(signed_distance_);
}

EditorAction ExtrudeTool::finishExtrude() {
    if (std::abs(signed_distance_) <= kMinimumExtrudeDistance) {
        return EditorAction::consumeEvent();
    }

    modeling::ExtrudeParams params{
        .profile = asset::toPlanarProfile(profile_),
        .direction = profile_.frame.normal,
        .distance = std::abs(signed_distance_),
        .inward = signed_distance_ < 0.0,
    };
    EditorAction action = EditorAction::commit(DocumentOperation::extrudeFace("Extrude", std::move(params)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

EditorAction ExtrudeTool::updateProfilePreview(const ToolPoint* cursor) const {
    const std::vector<math::Point3> points = profilePreviewPoints(cursor);
    if (points.size() < 2) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(
            DraftGeometry::curve(asset::CurvePrimitive::polyline(math::Polyline3(points, points.size() >= 3))));
}

EditorAction ExtrudeTool::updateExtrudePreview(double signedDistance) const {
    if (!profile_.hasOuterLoop()) {
        return EditorAction::clearPreview();
    }

    const math::Vec3 offset = profile_.frame.normal * signedDistance;
    std::vector<asset::CurvePrimitive> curves;
    curves.reserve(profile_.outer.points.size() + 2);
    curves.push_back(closedLoopCurve(profile_.outer.points));

    std::vector<math::Point3> topLoop;
    topLoop.reserve(profile_.outer.points.size());
    for (const math::Point3& point : profile_.outer.points) {
        topLoop.push_back(point + offset);
    }
    curves.push_back(closedLoopCurve(topLoop));

    for (size_t i = 0; i < profile_.outer.points.size(); ++i) {
        curves.push_back(asset::CurvePrimitive::segment(math::Segment3(profile_.outer.points[i], topLoop[i])));
    }
    return EditorAction::setPreview(DraftGeometry::curves(std::move(curves)));
}

ToolPoint ExtrudeTool::normalizePointToFrame(ToolPoint point) {
    ensureFrame(point);
    point.point.world = frame_.plane().project(point.world());
    return point;
}

void ExtrudeTool::ensureFrame(const ToolPoint& firstPoint) {
    if (!has_frame_) {
        frame_ = makeFrame(firstPoint);
        has_frame_ = true;
    }
}

bool ExtrudeTool::closesToFirst(const ToolPoint& point) const {
    if (acceptedPointCount() < 3) {
        return false;
    }
    const ToolPoint* first = firstAcceptedPoint();
    if (!first) {
        return false;
    }
    if (first->hasCursor && point.hasCursor) {
        const double dx = point.screenX - first->screenX;
        const double dy = point.screenY - first->screenY;
        if (dx * dx + dy * dy <= kCloseToStartPixels * kCloseToStartPixels) {
            return true;
        }
    }
    return first->world().distanceSq(point.world()) <= kMinimumEdgeLengthSq;
}

std::vector<math::Point3> ExtrudeTool::profilePreviewPoints(const ToolPoint* cursor) const {
    std::vector<math::Point3> points = acceptedWorldPoints();
    if (cursor && !closesToFirst(*cursor) &&
        (points.empty() || points.back().distanceSq(cursor->world()) > kMinimumEdgeLengthSq)) {
        points.push_back(cursor->world());
    }
    return asset::cleanFaceLoop(points);
}

std::optional<asset::FaceDefinition> ExtrudeTool::completedProfile() const {
    if (!has_frame_) {
        return std::nullopt;
    }
    std::vector<math::Point3> points = asset::cleanFaceLoop(acceptedWorldPoints());
    if (points.size() < 3) {
        return std::nullopt;
    }

    asset::FaceDefinition face{
        .frame = frame_,
        .outer = asset::FaceLoop{ .points = std::move(points) },
    };
    return asset::buildFaceSolidMesh(face).empty() ? std::nullopt
                                                   : std::optional<asset::FaceDefinition>(std::move(face));
}

double ExtrudeTool::signedDistanceFor(const EditorInput& input) const {
    // 轮廓由视图平面绘制，射线与轮廓平面的交点不会给出深度；因此以屏幕纵向位移
    // 映射为与轮廓尺度相称的世界距离。向上为法线，向下为负法线。
    return (extrusion_anchor_y_ - input.screenY) * profileScale() / kPixelsPerProfileExtent;
}

double ExtrudeTool::profileScale() const {
    if (profile_.outer.points.empty()) {
        return 1.0;
    }

    double minX = profile_.outer.points.front().x;
    double minY = profile_.outer.points.front().y;
    double minZ = profile_.outer.points.front().z;
    double maxX = minX;
    double maxY = minY;
    double maxZ = minZ;
    for (const math::Point3& point : profile_.outer.points) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        minZ = std::min(minZ, point.z);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
        maxZ = std::max(maxZ, point.z);
    }
    return std::max(1.0, math::Vec3(maxX - minX, maxY - minY, maxZ - minZ).length());
}

}  // namespace mulan::app
