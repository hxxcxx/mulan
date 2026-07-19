/**
 * @file editor_session.h
 * @brief 管理单个文档视图中的编辑工具会话。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "../selection/editor_input.h"
#include "../snap/editor_input_resolver.h"
#include "../grip/editor_grip_controller.h"
#include "editor_overlay_service.h"
#include "../selection/editor_pick_service.h"
#include "../selection/editor_selection.h"
#include "../selection/editor_selection_service.h"
#include "../tools/editor_tool.h"
#include "../operation/document_operation_executor.h"
#include "../tools/tool_controller.h"
#include "../operation/transform_edit_context.h"

#include <mulan/interaction/input_event.h>
#include <mulan/interaction/work_plane.h>

#include <memory>
#include <string_view>

namespace mulan::view {
class ViewContext;
}

namespace mulan::editor {

class DocumentSession;
class DocumentViewBinding;

class EditorToolOperator;

/// EditorToolOperator 驱动一次活动工具后的完整结果。
struct EditorToolDispatchResult {
    bool consumed = false;
    bool hadActiveTool = false;
    ToolLifecycle lifecycle = ToolLifecycle::Running;
};

class EditorSession {
public:
    EditorSession();
    ~EditorSession();

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    void bind(DocumentSession* session, view::ViewContext* view, DocumentViewBinding* binding);
    void unbind();

    bool isReady() const;
    bool hasActiveTool() const { return tool_controller_.hasActiveTool(); }
    std::string_view activeToolId() const {
        const EditorTool* tool = tool_controller_.activeTool();
        return tool ? tool->id() : std::string_view{};
    }

    void startTool(std::unique_ptr<EditorTool> tool);
    bool canStartTransformTool(TransformEditCommitMode commitMode) const;
    bool startTransformTool(TransformEditCommitMode commitMode);
    bool startSelectionExtrudeTool();
    bool deleteSelectedEntities();
    bool undo();
    bool redo();
    bool canUndo() const { return operation_executor_.canUndo(); }
    bool canRedo() const { return operation_executor_.canRedo(); }
    bool handleInput(const engine::InputEvent& event);

    /// 驱动当前活动工具处理一次输入（供 EditorToolOperator 适配器使用）。
    /// 完成 makeEditorInput → snap preview → tool dispatch → applyAction 全流程。
    /// 同时返回 consumed 与生命周期；Operator 据此区分正常完成和取消，不能再用
    /// “工具是否还存在”猜测 finish(true/false)。
    EditorToolDispatchResult driveActiveTool(const engine::InputEvent& event);

    /// 取消当前活动工具（供 EditorToolOperator 在 cancel 时调用）。
    void cancelActiveTool();
    void refreshGrips();
    void clearGrips();
    bool updateGripHoverAtFramebuffer(double screenX, double screenY);
    void clearGripHover();
    bool updateHoverAtFramebuffer(double screenX, double screenY);
    void selectAtFramebuffer(double screenX, double screenY);
    void clearHover();
    void setSelectionFilter(EditorSelectionFilter filter);
    const EditorSelectionContext& selectionContext() const { return selection_service_.context(); }
    void setWorkPlane(engine::WorkPlane plane);
    const engine::WorkPlane& workPlane() const;

private:
    friend class EditorToolOperator;

    EditorInput makeEditorInput(const engine::InputEvent& event) const;
    void updateSnapPreview(const EditorInput& input);
    bool tryStartGripDrag(const engine::InputEvent& event);
    bool applyAction(EditorAction action);
    void installToolOperator();
    void removeToolOperator();
    void cancelToolState();
    /// 由正在分发事件的 EditorToolOperator 调用：只取消工具状态，不在其成员函数
    /// 尚未返回时销毁 Operator；随后由 ViewContext 按 finished 状态安全弹栈。
    void cancelActiveToolFromOperator();

    DocumentSession* session_ = nullptr;
    view::ViewContext* view_ = nullptr;
    DocumentViewBinding* binding_ = nullptr;
    EditorInputResolver input_resolver_;
    EditorPickService pick_service_;
    EditorOverlayService overlay_service_;
    DocumentOperationExecutor operation_executor_;
    EditorGripController grip_controller_;
    EditorSelectionService selection_service_;
    ToolController tool_controller_;
    /// 非拥有指针；对象所有权属于 ViewContext，完成回调会在弹栈前清空本字段。
    EditorToolOperator* tool_operator_ = nullptr;
};

}  // namespace mulan::editor
