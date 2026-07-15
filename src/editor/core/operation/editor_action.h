/**
 * @file editor_action.h
 * @brief 定义编辑工具对编辑会话输出的动作。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "document_operation.h"
#include "draft_geometry.h"

#include <mulan/view/core/preview_layer.h>

#include <optional>
#include <vector>

namespace mulan::editor {

enum class ToolFinishReason {
    Finished,
    Cancelled,
    Replaced,
};

enum class ToolLifecycle {
    Running,
    Finished,
    Cancelled,
};

class EditorAction {
public:
    static EditorAction ignored();
    static EditorAction consumeEvent();
    static EditorAction setPreview(DraftGeometry geometry);
    static EditorAction setPreviewReferences(std::vector<view::PreviewReference> references);
    static EditorAction clearPreview();
    static EditorAction commit(DocumentOperation operation);
    /// 提交操作并结束工具：等价于 commit(op).clearPreviewOnApply().finishTool()。
    /// 所有编辑工具的 commit 路径共用此尾巴，集中于此避免重复。
    static EditorAction commitAndFinish(DocumentOperation operation);
    static EditorAction finish();
    static EditorAction cancel();

    bool isConsumed() const { return consumed_; }
    bool shouldClearPreview() const { return clear_preview_; }
    ToolLifecycle lifecycle() const { return lifecycle_; }

    std::optional<DraftGeometry>& preview() { return preview_; }
    const std::optional<DraftGeometry>& preview() const { return preview_; }
    std::vector<view::PreviewReference>& previewReferences() { return preview_references_; }
    const std::vector<view::PreviewReference>& previewReferences() const { return preview_references_; }
    bool hasPreviewReferences() const { return !preview_references_.empty(); }

    std::optional<DocumentOperation>& operation() { return operation_; }
    const std::optional<DocumentOperation>& operation() const { return operation_; }

    EditorAction& consume();
    EditorAction& clearPreviewOnApply();
    EditorAction& cancelTool();

private:
    bool consumed_ = false;
    bool clear_preview_ = false;
    ToolLifecycle lifecycle_ = ToolLifecycle::Running;
    std::optional<DraftGeometry> preview_;
    std::vector<view::PreviewReference> preview_references_;
    std::optional<DocumentOperation> operation_;
};

}  // namespace mulan::editor
