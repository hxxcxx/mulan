#include "core/tools/face_tool.h"

#include <cmath>
#include <utility>
#include <vector>

namespace mulan::app {
namespace {

constexpr double kMinimumEdgeLengthSq = 1.0e-12;
constexpr double kCloseToStartPixels = 10.0;

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

DraftGeometry buildFaceDraft(std::vector<math::Point3> points, const asset::FacePlaneFrame& frame) {
    points = asset::cleanFaceLoop(points);

    std::vector<asset::CurvePrimitive> curves;
    std::vector<graphics::Mesh> meshes;
    if (points.size() >= 2) {
        curves.push_back(asset::CurvePrimitive::polyline(math::Polyline3(points, points.size() >= 3)));
    }

    if (points.size() >= 3) {
        asset::FaceDefinition face{
            .frame = frame,
            .outer = asset::FaceLoop{ .points = points },
        };
        graphics::Mesh mesh = asset::buildFaceSolidMesh(face);
        if (!mesh.empty()) {
            meshes.push_back(std::move(mesh));
        }
    }

    return DraftGeometry::geometry(std::move(curves), std::move(meshes));
}

}  // namespace

EditorAction FaceTool::onPointPressed(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    ToolPoint normalized = normalizePointToFrame(point);
    if (closesToFirst(normalized)) {
        return finishFace();
    }
    return acceptPoint(std::move(normalized));
}

EditorAction FaceTool::onPointMoved(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    if (!hasAcceptedPoints()) {
        return EditorAction::consumeEvent();
    }

    ToolPoint normalized = normalizePointToFrame(point);
    return updatePreview(&normalized);
}

EditorAction FaceTool::onRightPressed(const EditorInput& input) {
    (void) input;
    return finishFace();
}

void FaceTool::resetDrawingState() {
    has_frame_ = false;
    frame_ = asset::FacePlaneFrame{};
}

EditorAction FaceTool::acceptPoint(ToolPoint point) {
    const ToolPoint* last = lastAcceptedPoint();
    if (last && last->world().distanceSq(point.world()) <= kMinimumEdgeLengthSq) {
        return updatePreview(nullptr);
    }

    addAcceptedPoint(std::move(point));
    return updatePreview(nullptr);
}

EditorAction FaceTool::finishFace() const {
    std::vector<math::Point3> accepted = acceptedWorldPoints();
    std::vector<math::Point3> points = asset::cleanFaceLoop(accepted);
    if (!has_frame_ || points.size() < 3) {
        return EditorAction::cancel();
    }

    asset::FaceDefinition face{
        .frame = frame_,
        .outer = asset::FaceLoop{ .points = std::move(points) },
    };

    if (asset::buildFaceSolidMesh(face).empty()) {
        return updatePreview(nullptr);
    }

    EditorAction action = EditorAction::commit(DocumentOperation::createFace("Face", std::move(face)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

EditorAction FaceTool::updatePreview(const ToolPoint* cursor) const {
    DraftGeometry geometry = previewGeometry(cursor);
    if (geometry.empty()) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(std::move(geometry));
}

ToolPoint FaceTool::normalizePointToFrame(ToolPoint point) {
    ensureFrame(point);
    point.point.world = frame_.plane().project(point.world());
    return point;
}

void FaceTool::ensureFrame(const ToolPoint& firstPoint) {
    if (has_frame_) {
        return;
    }

    frame_ = makeFrame(firstPoint);
    has_frame_ = true;
}

std::vector<math::Point3> FaceTool::previewPoints(const ToolPoint* cursor) const {
    std::vector<math::Point3> points = acceptedWorldPoints();
    if (cursor && !closesToFirst(*cursor) &&
        (points.empty() || points.back().distanceSq(cursor->world()) > kMinimumEdgeLengthSq)) {
        points.push_back(cursor->world());
    }
    return asset::cleanFaceLoop(points);
}

DraftGeometry FaceTool::previewGeometry(const ToolPoint* cursor) const {
    if (!has_frame_) {
        return DraftGeometry::curves({});
    }
    return buildFaceDraft(previewPoints(cursor), frame_);
}

bool FaceTool::closesToFirst(const ToolPoint& point) const {
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

}  // namespace mulan::app
