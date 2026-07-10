/**
 * @file document_view.cpp
 * @brief DocumentView 实现。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "document_view.h"

#include "document_session.h"

DocumentView::DocumentView() = default;

DocumentView::~DocumentView() {
    editor_session_.unbind();
    view_context_.clearPreview();
    binding_.unbind();
}

bool DocumentView::init(const mulan::view::ViewConfig& config, int width, int height) {
    if (view_context_.isInitialized()) {
        return true;
    }

    if (!view_context_.init(config, width, height)) {
        return false;
    }

    if (session_) {
        binding_.bind(*session_, view_context_);
        editor_session_.bind(session_, &view_context_, &binding_);
    }
    return true;
}

void DocumentView::resize(int width, int height) {
    if (view_context_.isInitialized()) {
        view_context_.resize(width, height);
        editor_session_.refreshGrips();
    }
}

void DocumentView::renderFrame() {
    if (view_context_.isInitialized()) {
        view_context_.renderFrame();
    }
}

void DocumentView::fitAll() {
    if (!view_context_.isInitialized()) {
        return;
    }

    binding_.fitAll();
    editor_session_.refreshGrips();
}

void DocumentView::setDocumentSession(DocumentSession* session) {
    editor_session_.unbind();
    view_context_.clearPreview();
    binding_.unbind();
    session_ = session;

    if (view_context_.isInitialized() && session_) {
        binding_.bind(*session_, view_context_);
        editor_session_.bind(session_, &view_context_, &binding_);
    }
}

bool DocumentView::handleInput(const mulan::engine::InputEvent& event) {
    if (editor_session_.handleInput(event)) {
        return true;
    }

    const bool consumed = view_context_.handleInput(event);
    if (consumed) {
        // 只依据实体更新时缓存的世界包围球更新投影，不会重算场景范围。
        binding_.updateCameraClipPlanes();
        editor_session_.refreshGrips();
    }
    return consumed;
}

void DocumentView::updateHoverAtFramebuffer(double x, double y) {
    editor_session_.updateHoverAtFramebuffer(x, y);
}

void DocumentView::selectAtFramebuffer(double x, double y) {
    editor_session_.selectAtFramebuffer(x, y);
}
