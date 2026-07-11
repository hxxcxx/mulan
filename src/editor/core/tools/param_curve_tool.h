/**
 * @file param_curve_tool.h
 * @brief Parametric curve drawing tool
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "point_drawing_tool.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mulan::editor {

enum class ParametricCurveToolKind {
    Bezier,
    BSpline,
    NURBS,
};

class ParametricCurveTool final : public PointDrawingTool {
public:
    explicit ParametricCurveTool(ParametricCurveToolKind kind) : kind_(kind) {}

    std::string_view id() const override;

private:
    EditorAction onPointPressed(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onPointMoved(const EditorInput& input, const ToolPoint& point) override;
    EditorAction onRightPressed(const EditorInput& input) override;

    EditorAction acceptPoint(const EditorInput& input, ToolPoint point);
    EditorAction updatePreview(const EditorInput& input, const ToolPoint& point) const;
    EditorAction finishCurve() const;
    DraftGeometry previewGeometry(const EditorInput* input, const std::optional<ToolPoint>& cursor) const;
    std::optional<asset::CurvePrimitive> makePrimitive(std::vector<math::Point3> points) const;
    std::string curveName() const;

    ParametricCurveToolKind kind_ = ParametricCurveToolKind::Bezier;
};

}  // namespace mulan::editor
