#include "polyline_tool.h"

#include <utility>
#include <vector>

namespace mulan::editor {
namespace {

constexpr double kMinimumSegmentLengthSq = 1.0e-12;

}  // namespace

EditorAction PolylineTool::onPointPressed(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    return acceptPoint(point);
}

EditorAction PolylineTool::onPointMoved(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    return updatePreview(point.world());
}

EditorAction PolylineTool::onRightPressed(const EditorInput& input) {
    (void) input;
    return finishPolyline();
}

EditorAction PolylineTool::acceptPoint(ToolPoint point) {
    const ToolPoint* last = lastAcceptedPoint();
    if (last && last->world().distanceSq(point.world()) <= kMinimumSegmentLengthSq) {
        return updatePreview(point.world());
    }

    addAcceptedPoint(std::move(point));
    DraftGeometry geometry = previewGeometry(std::nullopt);
    if (geometry.empty()) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(std::move(geometry));
}

EditorAction PolylineTool::updatePreview(const math::Point3& point) const {
    DraftGeometry geometry = previewGeometry(point);
    if (geometry.empty()) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(std::move(geometry));
}

EditorAction PolylineTool::finishPolyline() const {
    if (acceptedPointCount() < 2) {
        return EditorAction::cancel();
    }

    math::Polyline3 polyline(acceptedWorldPoints(), false);
    return EditorAction::commitAndFinish(
            DocumentOperation::createCurve("Polyline", asset::CurvePrimitive::polyline(polyline)));
}

DraftGeometry PolylineTool::previewGeometry(const std::optional<math::Point3>& cursor) const {
    std::vector<math::Point3> preview = acceptedWorldPoints();
    if (cursor && (preview.empty() || preview.back().distanceSq(*cursor) > kMinimumSegmentLengthSq)) {
        preview.push_back(*cursor);
    }

    if (preview.size() < 2) {
        return DraftGeometry::curves({});
    }

    return DraftGeometry::curve(asset::CurvePrimitive::polyline(math::Polyline3(std::move(preview), false)));
}

}  // namespace mulan::editor
