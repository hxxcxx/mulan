#pragma once

#include "core/tools/point_drawing_tool.h"

#include <mulan/asset/face_asset.h>

namespace mulan::app {

/// 两阶段体绘制：先采集闭合轮廓，再沿轮廓法线用鼠标确定拉伸距离。
class ExtrudeTool final : public PointDrawingTool {
public:
    std::string_view id() const override { return "draw.extrude"; }

private:
    enum class Phase {
        DrawingProfile,
        Extruding,
    };

    EditorAction onPointPressed(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onPointMoved(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onRightPressed(const EditorInput& input) override;
    void resetDrawingState() override;

    EditorAction acceptProfilePoint(ToolPoint point);
    EditorAction finishProfile(const EditorInput& input);
    EditorAction finishExtrude();
    EditorAction updateProfilePreview(const ToolPoint* cursor) const;
    EditorAction updateExtrudePreview(double signedDistance) const;

    ToolPoint normalizePointToFrame(ToolPoint point);
    void ensureFrame(const ToolPoint& firstPoint);
    bool closesToFirst(const ToolPoint& point) const;
    std::vector<math::Point3> profilePreviewPoints(const ToolPoint* cursor) const;
    std::optional<asset::FaceDefinition> completedProfile() const;
    double signedDistanceFor(const EditorInput& input) const;
    double profileScale() const;

    Phase phase_ = Phase::DrawingProfile;
    bool has_frame_ = false;
    asset::FacePlaneFrame frame_;
    asset::FaceDefinition profile_;
    double extrusion_anchor_y_ = 0.0;
    double signed_distance_ = 0.0;
};

}  // namespace mulan::app
