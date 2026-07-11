/**
 * @file transform_tool.h
 * @brief TransformTool 使用 TransformEditContext 执行统一变换编辑。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "core/tools/editor_tool.h"
#include "core/operation/transform_edit_context.h"

#include <optional>

namespace mulan::io {
class Document;
}

namespace mulan::app {

class TransformTool final : public EditorTool {
public:
    TransformTool(const io::Document* document, TransformEditContext context,
                  TransformEditMode mode = TransformEditMode::Translate,
                  TransformEditCommitMode commitMode = TransformEditCommitMode::Move);
    TransformTool(const io::Document* document, TransformEditContext context, math::Point3 dragStartWorld,
                  TransformEditMode mode = TransformEditMode::Translate,
                  TransformEditCommitMode commitMode = TransformEditCommitMode::Move);

    std::string_view id() const override { return "edit.transform"; }
    EditorPointPolicy pointPolicy() const override;
    EditorAction begin() override;
    EditorAction handleInput(const EditorInput& input) override;
    EditorAction end(ToolFinishReason reason) override;

private:
    EditorAction setDragStart(math::Point3 worldPoint);
    EditorAction update(const math::Point3& worldPoint);
    EditorAction commit(const math::Point3& worldPoint);
    EditorAction updatePreview(const math::Mat4& worldDelta) const;
    std::optional<math::Mat4> worldDelta(const math::Point3& worldPoint) const;

    TransformEditContext context_;
    TransformEditMode mode_ = TransformEditMode::Translate;
    TransformEditCommitMode commit_mode_ = TransformEditCommitMode::Move;
    std::optional<math::Point3> drag_start_world_;
    std::optional<math::Mat4> current_delta_;
    bool drag_preview_started_ = false;
};

}  // namespace mulan::app
