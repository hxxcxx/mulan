#pragma once

#include "../operation/drag_edit_session.h"
#include "../grip/editor_grip.h"
#include "drag_from_anchor_tool.h"

#include <optional>

namespace mulan::editor {

class GripDragTool final : public DragFromAnchorTool {
public:
    GripDragTool(EditorGrip grip, math::Point3 dragStartWorld);

    std::string_view id() const override { return "edit.gripDrag"; }
    EditorPointPolicy pointPolicy() const override;
    EditorAction begin() override;
    EditorAction end(ToolFinishReason reason) override;

protected:
    EditorAction onAnchorPress(const EditorInput& input, const math::Point3& worldPoint) override;
    EditorAction updateDragPreview(const EditorInput& input, const math::Point3& worldPoint) override;
    EditorAction commitAtPoint(const EditorInput& input, const math::Point3& worldPoint) override;
    std::optional<EditorAction> commitFallback(const EditorInput& input) override;

private:
    EditorAction commitPrimitive(const asset::CurvePrimitive& primitive);  ///< 由图元构造提交 action
    std::optional<asset::CurvePrimitive> makeEditedPrimitive(const DragEditSample& sample) const;
    DraftGeometry previewGeometry(const EditorInput& input, const asset::CurvePrimitive& primitive) const;

    EditorGrip grip_;
    DragEditSession drag_;
    std::optional<asset::CurvePrimitive> current_primitive_;
};

}  // namespace mulan::editor
