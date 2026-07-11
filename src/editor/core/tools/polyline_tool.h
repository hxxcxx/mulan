#pragma once

#include "core/tools/point_drawing_tool.h"

#include <optional>

namespace mulan::app {

class PolylineTool final : public PointDrawingTool {
public:
    std::string_view id() const override { return "draw.polyline"; }

private:
    EditorAction onPointPressed(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onPointMoved(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onRightPressed(const EditorInput& input) override;

    EditorAction acceptPoint(ToolPoint point);
    EditorAction updatePreview(const math::Point3& point) const;
    EditorAction finishPolyline() const;
    DraftGeometry previewGeometry(const std::optional<math::Point3>& cursor) const;
};

}  // namespace mulan::app
