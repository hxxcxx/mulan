/**
 * @file transform_tool.h
 * @brief TransformTool 使用 TransformEditContext 执行统一变换编辑。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "drag_from_anchor_tool.h"
#include "../operation/transform_edit_context.h"

#include <optional>

namespace mulan {
class Document;
}

namespace mulan::editor {

class TransformTool final : public DragFromAnchorTool {
public:
    TransformTool(const Document* document, TransformEditContext context,
                  TransformEditMode mode = TransformEditMode::Translate,
                  TransformEditCommitMode commitMode = TransformEditCommitMode::Move);
    TransformTool(const Document* document, TransformEditContext context, math::Point3 dragStartWorld,
                  TransformEditMode mode = TransformEditMode::Translate,
                  TransformEditCommitMode commitMode = TransformEditCommitMode::Move);

    std::string_view id() const override { return "edit.transform"; }
    EditorPointPolicy pointPolicy() const override;
    EditorAction begin() override;
    EditorAction end(ToolFinishReason reason) override;

protected:
    EditorAction onAnchorPress(const EditorInput& input, const math::Point3& worldPoint) override;
    EditorAction updateDragPreview(const EditorInput& input, const math::Point3& worldPoint) override;
    EditorAction commitAtPoint(const EditorInput& input, const math::Point3& worldPoint) override;
    std::optional<EditorAction> commitFallback(const EditorInput& input) override;

private:
    EditorAction setDragStart(math::Point3 worldPoint);
    EditorAction commit(const math::Point3& worldPoint);
    EditorAction commitWithLastDelta() const;  ///< release 时 worldPoint 缺失的回退提交
    EditorAction updatePreview(const math::Mat4& worldDelta) const;
    std::optional<math::Mat4> worldDelta(const math::Point3& worldPoint) const;

    TransformEditContext context_;
    TransformEditMode mode_ = TransformEditMode::Translate;
    TransformEditCommitMode commit_mode_ = TransformEditCommitMode::Move;
    std::optional<math::Point3> drag_start_world_;
    std::optional<math::Mat4> current_delta_;
    bool drag_preview_started_ = false;
    /// 仅显式拖动入口在 release 提交；普通 Move/Copy 命令始终由第二次 press 确认。
    bool commit_on_release_ = false;
};

}  // namespace mulan::editor
