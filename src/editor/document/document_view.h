/**
 * @file document_view.h
 * @brief 管理一个文档视图的会话绑定、视口运行时和文档渲染连接。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include "document_view_binding.h"

#include "../command/command.h"
#include "../core/session/editor_session.h"

#include <mulan/interaction/input_event.h>
#include <mulan/view/core/view_config.h>
#include <mulan/view/core/view_context.h>

class DocumentSession;

class DocumentView {
public:
    DocumentView();
    ~DocumentView();

    DocumentView(const DocumentView&) = delete;
    DocumentView& operator=(const DocumentView&) = delete;

    bool init(const mulan::view::ViewConfig& config, int width, int height);
    void resize(int width, int height);
    void renderFrame();
    void fitAll();

    bool isInitialized() const { return view_context_.isInitialized(); }

    void setDocumentSession(DocumentSession* session);
    DocumentSession* session() const { return session_; }

    mulan::view::ViewContext& viewContext() { return view_context_; }
    const mulan::view::ViewContext& viewContext() const { return view_context_; }

    DocumentViewBinding& binding() { return binding_; }
    const DocumentViewBinding& binding() const { return binding_; }

    bool handleInput(const mulan::engine::InputEvent& event);

    // ── 编辑器交互（转发，app 层不直接接触 EditorSession）──

    bool isEditorReady() const { return editor_session_.isReady(); }
    bool hasActiveEditorTool() const { return editor_session_.hasActiveTool(); }
    std::string_view activeEditorToolId() const { return editor_session_.activeToolId(); }
    void cancelActiveEditorTool() { editor_session_.cancelActiveTool(); }
    void clearEditorHover() { editor_session_.clearHover(); }
    bool canEditorUndo() const { return editor_session_.canUndo(); }
    bool canEditorRedo() const { return editor_session_.canRedo(); }

    /// 构造命令宿主，供 CommandManager 执行命令。
    mulan::editor::CommandHost commandHost() { return mulan::editor::CommandHost(this, &editor_session_); }

    void updateHoverAtFramebuffer(double x, double y);
    void selectAtFramebuffer(double x, double y);

private:
    DocumentSession* session_ = nullptr;
    DocumentViewBinding binding_;
    mulan::view::ViewContext view_context_;
    mulan::editor::EditorSession editor_session_;
};
