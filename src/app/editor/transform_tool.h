/**
 * @file transform_tool.h
 * @brief TransformTool 使用 TransformEditContext 执行统一变换编辑。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "editor_tool.h"
#include "transform_edit_context.h"

#include <optional>

namespace mulan::app {

class TransformTool final : public EditorTool {
public:
    TransformTool(TransformEditContext context, math::Point3 dragStartWorld,
                  TransformEditMode mode = TransformEditMode::Translate);

    std::string_view id() const override { return "edit.transform"; }
    EditorPointPolicy pointPolicy() const override;
    EditorAction begin() override;
    EditorAction handleInput(const EditorInput& input) override;
    EditorAction end(ToolFinishReason reason) override;

private:
    EditorAction update(const math::Point3& worldPoint);
    EditorAction commit(const math::Point3& worldPoint);
    std::optional<math::Mat4> worldDelta(const math::Point3& worldPoint) const;

    TransformEditContext context_;
    TransformEditMode mode_ = TransformEditMode::Translate;
    math::Point3 drag_start_world_;
    std::optional<math::Mat4> current_delta_;
};

}  // namespace mulan::app
