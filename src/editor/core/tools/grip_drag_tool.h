#pragma once

#include "../operation/drag_edit_session.h"
#include "../grip/editor_grip.h"
#include "editor_tool.h"

#include <optional>

namespace mulan::editor {

class GripDragTool final : public EditorTool {
public:
    GripDragTool(EditorGrip grip, math::Point3 dragStartWorld);

    std::string_view id() const override { return "edit.gripDrag"; }
    EditorPointPolicy pointPolicy() const override;
    EditorAction begin() override;
    EditorAction handleInput(const EditorInput& input) override;
    EditorAction end(ToolFinishReason reason) override;

private:
    EditorAction updatePreview(const EditorInput& input, const math::Point3& worldPoint);
    EditorAction commitAt(const math::Point3& worldPoint);
    std::optional<asset::CurvePrimitive> makeEditedPrimitive(const DragEditSample& sample) const;
    DraftGeometry previewGeometry(const EditorInput& input, const asset::CurvePrimitive& primitive) const;

    EditorGrip grip_;
    DragEditSession drag_;
    std::optional<asset::CurvePrimitive> current_primitive_;
};

}  // namespace mulan::editor
