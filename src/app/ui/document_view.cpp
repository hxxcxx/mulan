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
    }
}

void DocumentView::renderFrame() {
    if (view_context_.isInitialized()) {
        view_context_.renderFrame();
    }
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

    return view_context_.handleInput(event);
}

void DocumentView::updateHoverAtFramebuffer(double x, double y) {
    if (!binding_.isBound()) {
        view_context_.clearHoveredPickId();
        return;
    }

    const auto hit = binding_.pickEntityAt(view_context_.camera(), x, y);
    if (hit) {
        view_context_.setHoveredPickId(hit->pickId);
    } else {
        view_context_.clearHoveredPickId();
    }
}

void DocumentView::selectAtFramebuffer(double x, double y) {
    if (!binding_.isBound()) {
        return;
    }

    const auto hit = binding_.pickEntityAt(view_context_.camera(), x, y);
    if (hit) {
        view_context_.setHoveredPickId(hit->pickId);
        binding_.selectSingle(hit->entity);
    } else {
        view_context_.clearHoveredPickId();
        binding_.clearSelection();
    }
}
