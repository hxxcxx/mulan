#pragma once

#include "editor_tool.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace mulan::editor {

struct ToolPoint {
    EditorPoint point;
    engine::WorkPlane workPlane = engine::WorkPlane::worldXY();
    math::Ray3 cursorRay;
    std::optional<math::Point3> workPoint;
    std::optional<EditorPickHit> pickHit;
    std::optional<EditorGeometryDependency> geometryDependency;
    double screenX = 0.0;
    double screenY = 0.0;
    bool hasCursor = false;
    bool hasCursorRay = false;
    bool workPlaneHit = false;
    bool pickTested = false;
    bool snapResolved = false;

    static std::optional<ToolPoint> fromInput(const EditorInput& input);

    const math::Point3& world() const { return point.world; }
    EditorPointSource source() const { return point.source; }
    EditorSnapKind snapKind() const { return point.snapKind; }
    EditorPointDependencyKind dependency() const { return point.dependency; }
    bool dependsOnGeometry() const { return point.dependsOnGeometry() || geometryDependency.has_value(); }
};

class PointDrawingTool : public EditorTool {
public:
    EditorPointPolicy pointPolicy() const final;
    EditorAction begin() final;
    EditorAction handleInput(const EditorInput& input) final;
    EditorAction end(ToolFinishReason reason) final;

protected:
    const std::vector<ToolPoint>& acceptedPoints() const { return accepted_points_; }
    std::size_t acceptedPointCount() const { return accepted_points_.size(); }
    bool hasAcceptedPoints() const { return !accepted_points_.empty(); }
    const ToolPoint* firstAcceptedPoint() const;
    const ToolPoint* lastAcceptedPoint() const;
    const ToolPoint& addAcceptedPoint(ToolPoint point);
    void clearAcceptedPoints();
    std::vector<math::Point3> acceptedWorldPoints() const;

    virtual EditorAction onPointPressed(const EditorInput& input, const ToolPoint& point) = 0;
    virtual EditorAction onPointMoved(const EditorInput& input, const ToolPoint& point);
    virtual EditorAction onRightPressed(const EditorInput& input);
    virtual std::optional<math::Point3> axisAnchor() const;
    virtual void configurePointPolicy(EditorPointPolicy& policy) const;
    virtual void resetDrawingState();

private:
    std::vector<ToolPoint> accepted_points_;
};

}  // namespace mulan::editor
