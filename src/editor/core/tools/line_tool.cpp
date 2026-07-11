#include "line_tool.h"

#include <utility>

namespace mulan::editor {
namespace {

constexpr double kMinimumLineLengthSq = 1.0e-12;

}  // namespace

EditorAction LineTool::onPointPressed(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    if (!hasAcceptedPoints()) {
        return acceptStartPoint(point);
    }
    return acceptEndPoint(point);
}

EditorAction LineTool::onPointMoved(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    if (acceptedPointCount() != 1) {
        return EditorAction::consumeEvent();
    }
    return updateRubberBand(point);
}

EditorAction LineTool::acceptStartPoint(ToolPoint point) {
    addAcceptedPoint(std::move(point));
    return EditorAction::clearPreview();
}

EditorAction LineTool::acceptEndPoint(const ToolPoint& point) {
    const ToolPoint* first = firstAcceptedPoint();
    if (!first) {
        return EditorAction::clearPreview();
    }

    const math::Segment3 segment(first->world(), point.world());
    if (segment.lengthSq() <= kMinimumLineLengthSq) {
        return updateRubberBand(point);
    }

    EditorAction action =
            EditorAction::commit(DocumentOperation::createCurve("Line", asset::CurvePrimitive::segment(segment)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

EditorAction LineTool::updateRubberBand(const ToolPoint& point) const {
    const ToolPoint* first = firstAcceptedPoint();
    if (!first) {
        return EditorAction::consumeEvent();
    }

    const math::Segment3 segment(first->world(), point.world());
    if (segment.lengthSq() <= kMinimumLineLengthSq) {
        return EditorAction::clearPreview();
    }

    return EditorAction::setPreview(DraftGeometry::segment(segment));
}

}  // namespace mulan::editor
