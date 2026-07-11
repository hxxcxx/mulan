#pragma once

#include "point_drawing_tool.h"

#include <optional>

namespace mulan::app {

class CircleTool final : public PointDrawingTool {
public:
    std::string_view id() const override { return "draw.circle"; }

private:
    EditorAction onPointPressed(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onPointMoved(const EditorInput& input, const ToolPoint& point) override;

    EditorAction acceptCenter(ToolPoint point);
    EditorAction acceptRadiusPoint(const EditorInput& input, const ToolPoint& point) const;
    EditorAction updatePreview(const EditorInput& input, const ToolPoint& point) const;
    std::optional<math::Circle3> makeCircle(const EditorInput& input, const math::Point3& radiusPoint) const;
};

}  // namespace mulan::app
