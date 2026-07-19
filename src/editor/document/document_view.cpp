/**
 * @file document_view.cpp
 * @brief DocumentView 实现。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "document_view.h"

#include "document_view_binding.h"
#include "document_session.h"
#include "../core/session/editor_session.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/interaction/input_event.h>
#include <mulan/view/core/view_config.h>
#include <mulan/view/core/view_context.h>

#include <utility>

namespace mulan::editor {

struct DocumentView::Impl {
    DocumentSession* session = nullptr;
    DocumentViewBinding binding;
    mulan::view::ViewContext view_context;
    EditorSession editor_session;
    std::function<void()> frame_invalidation_callback;

    // 左键 click/drag/select 跟踪（从 DocumentViewport 下移；使用与 Qt drag threshold 相同的语义）。
    int left_press_x = 0;
    int left_press_y = 0;
    bool left_press_pending = false;
    bool left_press_dragged = false;
};

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

DocumentView::DocumentView() : impl_(std::make_unique<Impl>()) {
    impl_->binding.setFrameInvalidationCallback([this]() { invalidateFrame(); });
}

DocumentView::~DocumentView() {
    impl_->editor_session.unbind();
    impl_->view_context.clearPreview();
    impl_->binding.unbind();
}

bool DocumentView::init(const mulan::view::ViewConfig& config, int width, int height,
                        std::function<void()> runtimeEventCallback) {
    MULAN_PROFILE_ZONE();

    if (impl_->view_context.isReady()) {
        return true;
    }

    if (!impl_->view_context.init(config, width, height, std::move(runtimeEventCallback))) {
        LOG_ERROR("[Editor] Document view initialization failed: name={}, size={}x{}",
                  impl_->session ? std::string_view(impl_->session->displayName()) : std::string_view("<unbound>"),
                  width, height);
        return false;
    }

    if (impl_->session) {
        impl_->binding.bind(*impl_->session, impl_->view_context);
        impl_->editor_session.bind(impl_->session, &impl_->view_context, &impl_->binding);
    }
    LOG_INFO("[Editor] Document view initialized: name={}, size={}x{}",
             impl_->session ? std::string_view(impl_->session->displayName()) : std::string_view("<unbound>"), width,
             height);
    invalidateFrame();
    return true;
}

void DocumentView::resize(int width, int height) {
    if (impl_->view_context.isReady()) {
        impl_->view_context.resize(width, height);
        impl_->editor_session.refreshGrips();
        invalidateFrame();
    }
}

mulan::ResultVoid DocumentView::renderFrame() {
    const bool interactionActive = impl_->editor_session.hasActiveTool() || impl_->view_context.isCameraNavigating();
    impl_->binding.prepareFrame(interactionActive ? ClipUpdateMode::Interactive : ClipUpdateMode::Settled);
    return impl_->view_context.renderFrame();
}

mulan::ResultVoid DocumentView::consumeRenderEvents() {
    return impl_->view_context.consumeRenderEvents();
}

void DocumentView::fitAll() {
    if (!impl_->view_context.isReady()) {
        return;
    }

    impl_->binding.fitAll();
    impl_->editor_session.refreshGrips();
}

void DocumentView::setCameraToWorldXY() {
    if (!impl_->view_context.isReady()) {
        return;
    }

    impl_->view_context.setCameraToWorldXY();
    impl_->editor_session.refreshGrips();
    invalidateFrame();
}

void DocumentView::setFrameInvalidationCallback(std::function<void()> callback) {
    impl_->frame_invalidation_callback = std::move(callback);
}

bool DocumentView::isReady() const {
    return impl_->view_context.isReady();
}

DocumentSession* DocumentView::session() const {
    return impl_->session;
}

mulan::view::ViewContext& DocumentView::viewContext() {
    return impl_->view_context;
}

const mulan::view::ViewContext& DocumentView::viewContext() const {
    return impl_->view_context;
}

bool DocumentView::isEditorReady() const {
    return impl_->editor_session.isReady();
}

bool DocumentView::hasActiveEditorTool() const {
    return impl_->editor_session.hasActiveTool();
}

std::string_view DocumentView::activeEditorToolId() const {
    return impl_->editor_session.activeToolId();
}

void DocumentView::clearEditorHover() {
    impl_->editor_session.clearHover();
}

bool DocumentView::canEditorUndo() const {
    return impl_->editor_session.canUndo();
}

bool DocumentView::canEditorRedo() const {
    return impl_->editor_session.canRedo();
}

CommandHost DocumentView::commandHost() {
    return CommandHost(this, &impl_->editor_session);
}

void DocumentView::setDocumentSession(DocumentSession* session) {
    MULAN_PROFILE_ZONE();

    clearClickTracking();
    impl_->editor_session.unbind();
    impl_->view_context.clearPreview();
    impl_->binding.unbind();
    impl_->session = session;
    LOG_DEBUG("[Editor] Document view session changed: name={}",
              impl_->session ? std::string_view(impl_->session->displayName()) : std::string_view("<none>"));

    if (impl_->view_context.isReady() && impl_->session) {
        impl_->binding.bind(*impl_->session, impl_->view_context);
        impl_->editor_session.bind(impl_->session, &impl_->view_context, &impl_->binding);
    }
    invalidateFrame();
}

DocumentInputOutcome DocumentView::handleInput(const mulan::engine::InputEvent& event) {
    DocumentInputOutcome result;
    const bool hadActiveTool = impl_->editor_session.hasActiveTool();

    // 取消事件优先：统一通知 editor 与 view 两端清理临时交互。
    if (event.isCancelEvent()) {
        clearClickTracking();

        // 先让栈顶 Operator 在自身调用栈内完成“标记结束”，ViewContext 会在返回后
        // 安全弹栈。若工具与 Operator 状态意外失配，再由 Session 做幂等兜底清理。
        impl_->view_context.dispatchInput(event);
        if (impl_->editor_session.hasActiveTool()) {
            impl_->editor_session.cancelActiveTool();
        }
        impl_->editor_session.clearHover();

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

    const bool editorInteractionStarted = impl_->editor_session.handleInput(event);
    mulan::engine::InputOutcome viewOutcome = mulan::engine::InputOutcome::ignored();
    if (editorInteractionStarted) {
        // Grip press 已转交给新工具，原左键事务不能在后续 release 时退化成选择。
        clearClickTracking();
        result.disposition = DocumentInputDisposition::EditorTool;
        result.commandStateInvalidated = true;
    } else {
        // 视图段：栈顶可能是 EditorToolOperator 或默认 CameraManipulator。
        viewOutcome = impl_->view_context.dispatchInput(event);
        result.disposition = documentDisposition(viewOutcome.disposition, hadActiveTool);

        if (viewOutcome.disposition == mulan::engine::InputDisposition::ViewNavigation ||
            viewOutcome.disposition == mulan::engine::InputDisposition::ViewOverlay) {
            impl_->editor_session.refreshGrips();
        }

        const bool toolEnded = hadActiveTool && !impl_->editor_session.hasActiveTool();
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

    // Hover 也属于文档交互，不再由 DocumentViewport 根据 consumed 猜测是否执行。
    if (event.type == mulan::engine::InputEvent::Type::MouseMove && event.buttons == mulan::engine::MouseButton::None) {
        if (impl_->view_context.hasHoveredViewCubeFace() || impl_->editor_session.hasActiveTool()) {
            impl_->editor_session.clearHover();
        } else {
            impl_->editor_session.updateHoverAtFramebuffer(static_cast<double>(event.x), static_cast<double>(event.y));
        }
        result.frameInvalidated = true;
    }

    result.frameInvalidated = result.frameInvalidated || editorInteractionStarted || viewOutcome.handled() ||
                              result.disposition == DocumentInputDisposition::Selection;
    return result;
}

void DocumentView::trackPressEvent(const mulan::engine::InputEvent& event) {
    if (event.type == mulan::engine::InputEvent::Type::MousePress && event.button == mulan::engine::MouseButton::Left) {
        impl_->left_press_x = event.x;
        impl_->left_press_y = event.y;
        impl_->left_press_pending = true;
        impl_->left_press_dragged = false;
    } else if (event.type == mulan::engine::InputEvent::Type::MouseMove && impl_->left_press_pending) {
        if (isLeftDragExceedingThreshold(event)) {
            impl_->left_press_dragged = true;
        }
    }
}

bool DocumentView::maybeSelectOnRelease(const mulan::engine::InputEvent& event, bool allowSelection) {
    using T = mulan::engine::InputEvent::Type;
    if (event.type != T::MouseRelease || event.button != mulan::engine::MouseButton::Left ||
        !impl_->left_press_pending) {
        return false;
    }

    const bool shouldSelect =
            allowSelection && !impl_->left_press_dragged && !impl_->view_context.hasHoveredViewCubeFace();
    clearClickTracking();
    if (shouldSelect) {
        selectAtFramebuffer(static_cast<double>(event.x), static_cast<double>(event.y));
        return true;
    }

    return false;
}

void DocumentView::clearClickTracking() {
    impl_->left_press_pending = false;
    impl_->left_press_dragged = false;
}

bool DocumentView::isLeftDragExceedingThreshold(const mulan::engine::InputEvent& event) const {
    // 阈值使用 framebuffer 坐标；4 像素对应原 DocumentViewport logical 阈值在 DPR=1 下的行为。
    // 后续可改为从 QtViewportInputAdapter 传入 QApplication::startDragDistance()。
    const int dx = event.x - impl_->left_press_x;
    const int dy = event.y - impl_->left_press_y;
    const int threshold = 4;
    return (dx * dx + dy * dy) > (threshold * threshold);
}

void DocumentView::updateHoverAtFramebuffer(double x, double y) {
    impl_->editor_session.updateHoverAtFramebuffer(x, y);
}

void DocumentView::selectAtFramebuffer(double x, double y) {
    impl_->editor_session.selectAtFramebuffer(x, y);
}

void DocumentView::cancelActiveEditorTool() {
    impl_->editor_session.cancelActiveTool();
    clearClickTracking();
    invalidateFrame();
}

DocumentInputOutcome DocumentView::cancelInteraction() {
    // FocusLost 统一走 handleInput 的生命周期路径：工具、camera delegate、默认相机、
    // ViewCube click 事务与文档 click tracker 在同一个边界内清理。
    return handleInput(mulan::engine::InputEvent::focusLost());
}

void DocumentView::invalidateFrame() const {
    if (impl_->frame_invalidation_callback) {
        impl_->frame_invalidation_callback();
    }
}

}  // namespace mulan::editor
