#pragma once

#include "point_drawing_tool.h"

#include <mulan/asset/face_asset.h>

namespace mulan::editor {

class FaceTool final : public PointDrawingTool {
public:
    std::string_view id() const override { return "draw.face"; }

private:
    EditorAction onPointPressed(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onPointMoved(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onRightPressed(const EditorInput& input) override;
    void resetDrawingState() override;

    EditorAction acceptPoint(ToolPoint point);
    EditorAction finishFace() const;
    EditorAction updatePreview(const ToolPoint* cursor) const;

    ToolPoint normalizePointToFrame(ToolPoint point);
    void ensureFrame(const ToolPoint& firstPoint);
    std::vector<math::Point3> previewPoints(const ToolPoint* cursor) const;
    DraftGeometry previewGeometry(const ToolPoint* cursor) const;
    bool closesToFirst(const ToolPoint& point) const;

    bool has_frame_ = false;
    asset::FacePlaneFrame frame_;
};

}  // namespace mulan::editor
