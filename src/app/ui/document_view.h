/**
 * @file document_view.h
 * @brief 管理一个文档视图的会话绑定、视口运行时和文档渲染连接。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include "document_view_binding.h"

#include <mulan/engine/interaction/input_event.h>
#include <mulan/view/view_config.h>
#include <mulan/view/view_context.h>

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

    bool isInitialized() const { return view_context_.isInitialized(); }

    void setDocumentSession(DocumentSession* session);
    DocumentSession* session() const { return session_; }

    mulan::view::ViewContext& viewContext() { return view_context_; }
    const mulan::view::ViewContext& viewContext() const { return view_context_; }

    DocumentViewBinding& binding() { return binding_; }
    const DocumentViewBinding& binding() const { return binding_; }

    void handleInput(const mulan::engine::InputEvent& event);

    void updateHoverAtFramebuffer(double x, double y);
    void selectAtFramebuffer(double x, double y);
    bool hasModalOperator() const;

private:
    DocumentSession* session_ = nullptr;
    DocumentViewBinding binding_;
    mulan::view::ViewContext view_context_;
};
