/**
 * @file document_view.cpp
 * @brief DocumentView 实现。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "document_view.h"

#include "document_session.h"

#include <mulan/core/log/log.h>

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
        LOG_ERROR("[Editor] Document view initialization failed: name={}, size={}x{}",
                  session_ ? std::string_view(session_->displayName()) : std::string_view("<unbound>"), width, height);
        return false;
    }

    if (session_) {
        binding_.bind(*session_, view_context_);
        editor_session_.bind(session_, &view_context_, &binding_);
    }
    LOG_INFO("[Editor] Document view initialized: name={}, size={}x{}",
             session_ ? std::string_view(session_->displayName()) : std::string_view("<unbound>"), width, height);
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
    LOG_DEBUG("[Editor] Document view session changed: name={}",
              session_ ? std::string_view(session_->displayName()) : std::string_view("<none>"));

    if (view_context_.isInitialized() && session_) {
        binding_.bind(*session_, view_context_);
        editor_session_.bind(session_, &view_context_, &binding_);
    }
}

bool DocumentView::handleInput(const mulan::engine::InputEvent& event) {
    // 取消事件优先：统一通知 editor 与 view 两端清理临时交互。
    if (event.isCancelEvent()) {
        editor_session_.cancelActiveTool();
        left_press_pending_ = false;
        left_press_dragged_ = false;
        const bool consumed = view_context_.handleInput(event);  // activeOperator->cancel()
        return consumed;
    }

    // 跟踪左键 press，用于 release 时的 click-vs-drag 选择判定。
    trackPressEvent(event);

    // 编辑器段：无活动工具时处理 grip 启动。短路返回 true 时不再下行到 view 段。
    if (editor_session_.handleInput(event)) {
        return true;
    }

    // 视图段：栈分发。栈顶可能是 EditorToolOperator（工具激活时）或 CameraManipulator。
    const bool consumed = view_context_.handleInput(event);
    if (consumed) {
        binding_.updateCameraClipPlanes();
        editor_session_.refreshGrips();
    }

    // release 时判定是否执行选择（未被编辑器/视图消费且未拖动）。
    maybeSelectOnRelease(event, false);
    return consumed;
}

void DocumentView::trackPressEvent(const mulan::engine::InputEvent& event) {
    if (event.type == mulan::engine::InputEvent::Type::MousePress &&
        event.button == mulan::engine::MouseButton::Left) {
        left_press_x_ = event.x;
        left_press_y_ = event.y;
        left_press_pending_ = true;
        left_press_dragged_ = false;
    } else if (event.type == mulan::engine::InputEvent::Type::MouseMove && left_press_pending_) {
        if (isLeftDragExceedingThreshold(event)) {
            left_press_dragged_ = true;
        }
    }
}

void DocumentView::maybeSelectOnRelease(const mulan::engine::InputEvent& event, bool editorConsumed) {
    using T = mulan::engine::InputEvent::Type;
    if (event.type != T::MouseRelease || event.button != mulan::engine::MouseButton::Left ||
        !left_press_pending_) {
        return;
    }
    // 仅在未拖动、未被活动工具消费、且不悬停 ViewCube 时执行选择。
    if (!left_press_dragged_ && !editorConsumed && !view_context_.hasHoveredViewCubeFace()) {
        selectAtFramebuffer(static_cast<double>(event.x), static_cast<double>(event.y));
    }
    left_press_pending_ = false;
    left_press_dragged_ = false;
}

bool DocumentView::isLeftDragExceedingThreshold(const mulan::engine::InputEvent& event) const {
    // 阈值使用 framebuffer 坐标；4 像素对应原 DocWidget logical 阈值在 DPR=1 下的行为。
    // 后续可改为从 QtViewportInputAdapter 传入 QApplication::startDragDistance()。
    const int dx = event.x - left_press_x_;
    const int dy = event.y - left_press_y_;
    const int threshold = 4;
    return (dx * dx + dy * dy) > (threshold * threshold);
}

void DocumentView::updateHoverAtFramebuffer(double x, double y) {
    editor_session_.updateHoverAtFramebuffer(x, y);
}

void DocumentView::selectAtFramebuffer(double x, double y) {
    editor_session_.selectAtFramebuffer(x, y);
}

void DocumentView::cancelInteraction() {
    // 发送 FocusLost 事件统一走 cancel 路径：editor 取消工具，view 清理 camera drag / ViewCube。
    const mulan::engine::InputEvent cancelEvent = mulan::engine::InputEvent::focusLost();
    editor_session_.cancelActiveTool();
    editor_session_.clearHover();
    view_context_.handleInput(cancelEvent);
}
