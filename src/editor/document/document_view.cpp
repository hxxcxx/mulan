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

namespace {

DocumentInputDisposition documentDisposition(mulan::engine::InputDisposition disposition, bool editorToolActive) {
    using EngineDisposition = mulan::engine::InputDisposition;
    switch (disposition) {
    case EngineDisposition::Ignored: return DocumentInputDisposition::Ignored;
    case EngineDisposition::ViewNavigation: return DocumentInputDisposition::ViewNavigation;
    case EngineDisposition::ViewOverlay: return DocumentInputDisposition::ViewOverlay;
    case EngineDisposition::ModalInteraction:
        return editorToolActive ? DocumentInputDisposition::EditorTool : DocumentInputDisposition::ModalInteraction;
    case EngineDisposition::Cancelled: return DocumentInputDisposition::Cancelled;
    }
    return DocumentInputDisposition::Ignored;
}

}  // namespace

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
    clearClickTracking();
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

DocumentInputOutcome DocumentView::handleInput(const mulan::engine::InputEvent& event) {
    DocumentInputOutcome result;
    const bool hadActiveTool = editor_session_.hasActiveTool();

    // 取消事件优先：统一通知 editor 与 view 两端清理临时交互。
    if (event.isCancelEvent()) {
        clearClickTracking();

        // 先让栈顶 Operator 在自身调用栈内完成“标记结束”，ViewContext 会在返回后
        // 安全弹栈。若工具与 Operator 状态意外失配，再由 Session 做幂等兜底清理。
        view_context_.dispatchInput(event);
        if (editor_session_.hasActiveTool()) {
            editor_session_.cancelActiveTool();
        }
        editor_session_.clearHover();

        result.disposition = DocumentInputDisposition::Cancelled;
        result.frameInvalidated = true;
        result.commandStateInvalidated = hadActiveTool;
        return result;
    }

    // 跟踪左键 press，用于 release 时的 click-vs-drag 选择判定。
    // 活动工具拥有自己的左键事务，不应创建文档选择 click tracker。
    if (!hadActiveTool) {
        trackPressEvent(event);
    }

    const bool editorInteractionStarted = editor_session_.handleInput(event);
    mulan::engine::InputOutcome viewOutcome = mulan::engine::InputOutcome::ignored();
    if (editorInteractionStarted) {
        // Grip press 已转交给新工具，原左键事务不能在后续 release 时退化成选择。
        clearClickTracking();
        result.disposition = DocumentInputDisposition::EditorTool;
        result.commandStateInvalidated = true;
    } else {
        // 视图段：栈顶可能是 EditorToolOperator 或默认 CameraManipulator。
        viewOutcome = view_context_.dispatchInput(event);
        result.disposition = documentDisposition(viewOutcome.disposition, hadActiveTool);

        if (viewOutcome.disposition == mulan::engine::InputDisposition::ViewNavigation ||
            viewOutcome.disposition == mulan::engine::InputDisposition::ViewOverlay) {
            binding_.updateCameraClipPlanes();
            editor_session_.refreshGrips();
        }

        const bool toolEnded = hadActiveTool && !editor_session_.hasActiveTool();
        result.commandStateInvalidated = result.disposition == DocumentInputDisposition::EditorTool || toolEnded;

        // 未拖动的默认相机左键 press/release 仍应形成选择；模态工具与 ViewCube
        // 的 release 明确禁止落回选择，这是强类型 disposition 的核心用途。
        const bool allowSelection = viewOutcome.disposition == mulan::engine::InputDisposition::Ignored ||
                                    viewOutcome.disposition == mulan::engine::InputDisposition::ViewNavigation;
        if (maybeSelectOnRelease(event, allowSelection && !hadActiveTool)) {
            result.disposition = DocumentInputDisposition::Selection;
            result.commandStateInvalidated = true;
        }

        if (toolEnded) {
            clearClickTracking();
        }
    }

    // Hover 也属于文档交互，不再由 DocWidget 根据 consumed 猜测是否执行。
    if (event.type == mulan::engine::InputEvent::Type::MouseMove && event.buttons == mulan::engine::MouseButton::None) {
        if (view_context_.hasHoveredViewCubeFace() || editor_session_.hasActiveTool()) {
            editor_session_.clearHover();
        } else {
            editor_session_.updateHoverAtFramebuffer(static_cast<double>(event.x), static_cast<double>(event.y));
        }
        result.frameInvalidated = true;
    }

    result.frameInvalidated = result.frameInvalidated || editorInteractionStarted || viewOutcome.handled() ||
                              result.disposition == DocumentInputDisposition::Selection;
    return result;
}

void DocumentView::trackPressEvent(const mulan::engine::InputEvent& event) {
    if (event.type == mulan::engine::InputEvent::Type::MousePress && event.button == mulan::engine::MouseButton::Left) {
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

bool DocumentView::maybeSelectOnRelease(const mulan::engine::InputEvent& event, bool allowSelection) {
    using T = mulan::engine::InputEvent::Type;
    if (event.type != T::MouseRelease || event.button != mulan::engine::MouseButton::Left || !left_press_pending_) {
        return false;
    }

    const bool shouldSelect = allowSelection && !left_press_dragged_ && !view_context_.hasHoveredViewCubeFace();
    clearClickTracking();
    if (shouldSelect) {
        selectAtFramebuffer(static_cast<double>(event.x), static_cast<double>(event.y));
        return true;
    }

    return false;
}

void DocumentView::clearClickTracking() {
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

void DocumentView::cancelActiveEditorTool() {
    editor_session_.cancelActiveTool();
    clearClickTracking();
}

DocumentInputOutcome DocumentView::cancelInteraction() {
    // FocusLost 统一走 handleInput 的生命周期路径：工具、camera delegate、默认相机、
    // ViewCube click 事务与文档 click tracker 在同一个边界内清理。
    return handleInput(mulan::engine::InputEvent::focusLost());
}
