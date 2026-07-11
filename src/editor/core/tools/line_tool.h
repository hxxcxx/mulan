#pragma once

#include "core/tools/point_drawing_tool.h"

namespace mulan::app {

class LineTool final : public PointDrawingTool {
public:
    std::string_view id() const override { return "draw.line"; }

private:
    EditorAction onPointPressed(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onPointMoved(const EditorInput& input, const ToolPoint& point) override;

    EditorAction acceptStartPoint(ToolPoint point);
    EditorAction acceptEndPoint(const ToolPoint& point);
    EditorAction updateRubberBand(const ToolPoint& point) const;
};

}  // namespace mulan::app
