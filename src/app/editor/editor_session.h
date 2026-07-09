/**
 * @file editor_session.h
 * @brief 管理单个文档视图中的编辑工具会话。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "editor_input.h"
#include "editor_input_resolver.h"
#include "editor_grip_controller.h"
#include "editor_overlay_service.h"
#include "editor_pick_service.h"
#include "editor_selection.h"
#include "editor_tool.h"
#include "document_operation_executor.h"
#include "tool_controller.h"
#include "transform_edit_context.h"

#include <mulan/engine/interaction/input_event.h>
#include <mulan/engine/interaction/work_plane.h>

#include <memory>

class DocumentSession;
class DocumentViewBinding;

namespace mulan::view {
class ViewContext;
}

namespace mulan::app {

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

    void startTool(std::unique_ptr<EditorTool> tool);
    bool canStartTransformTool(TransformEditCommitMode commitMode) const;
    bool startTransformTool(TransformEditCommitMode commitMode);
    bool undo();
    bool redo();
    bool canUndo() const { return operation_executor_.canUndo(); }
    bool canRedo() const { return operation_executor_.canRedo(); }
    bool handleInput(const engine::InputEvent& event);
    void cancelActiveTool();
    void refreshGrips();
    void clearGrips();
    bool updateGripHoverAtFramebuffer(double screenX, double screenY);
    void clearGripHover();
    bool updateHoverAtFramebuffer(double screenX, double screenY);
    void selectAtFramebuffer(double screenX, double screenY);
    void clearHover();
    void setSelectionFilter(EditorSelectionFilter filter);
    const EditorSelectionContext& selectionContext() const { return selection_context_; }
    void setWorkPlane(engine::WorkPlane plane);
    const engine::WorkPlane& workPlane() const;

private:
    EditorInput makeEditorInput(const engine::InputEvent& event) const;
    void updateSnapPreview(const EditorInput& input);
    void syncSelectionVisualState();
    bool tryStartGripDrag(const engine::InputEvent& event);
    bool applyAction(EditorAction action);

    DocumentSession* session_ = nullptr;
    view::ViewContext* view_ = nullptr;
    DocumentViewBinding* binding_ = nullptr;
    EditorInputResolver input_resolver_;
    EditorPickService pick_service_;
    EditorOverlayService overlay_service_;
    DocumentOperationExecutor operation_executor_;
    EditorGripController grip_controller_;
    EditorSelectionContext selection_context_;
    ToolController tool_controller_;
};

}  // namespace mulan::app
