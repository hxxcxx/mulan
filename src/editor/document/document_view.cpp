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

namespace {

class PreviewClearGuard {
public:
    explicit PreviewClearGuard(mulan::view::ViewContext& view) : view_(view) {}
    ~PreviewClearGuard() { view_.clearPreview(); }

private:
    mulan::view::ViewContext& view_;
};

/// 一个 DocumentSession 挂接到一个已初始化 ViewContext 后形成的完整编辑环境。
/// 成员逆序析构保证 EditorSession → Preview → Binding 的安全拆除顺序。
class DocumentViewAttachment {
public:
    DocumentViewAttachment(DocumentSession& session, mulan::view::ViewContext& view,
                           std::function<void()> frameInvalidationCallback)
        : binding_(session, view, std::move(frameInvalidationCallback)),
          preview_clear_(view),
          editor_session_(session, view, binding_) {}

    ~DocumentViewAttachment() = default;

    DocumentViewAttachment(const DocumentViewAttachment&) = delete;
    DocumentViewAttachment& operator=(const DocumentViewAttachment&) = delete;

    DocumentViewBinding& binding() { return binding_; }
    EditorSession& editorSession() { return editor_session_; }

private:
    DocumentViewBinding binding_;
    PreviewClearGuard preview_clear_;
    EditorSession editor_session_;
};

}  // namespace

struct DocumentView::Impl {
    DocumentSession* session = nullptr;
    mulan::view::ViewContext view_context;
    std::unique_ptr<DocumentViewAttachment> attachment;
    std::function<void()> frame_invalidation_callback;

    // 左键 click/drag/select 跟踪（从 DocumentViewport 下移；使用与 Qt drag threshold 相同的语义）。
    int left_press_x = 0;
    int left_press_y = 0;
    bool left_press_pending = false;
    bool left_press_dragged = false;
    mulan::engine::MouseButton pressed_pointer_buttons = mulan::engine::MouseButton::None;
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
}

DocumentView::~DocumentView() {
    shutdown();
}

void DocumentView::shutdown() {
    clearClickTracking();
    impl_->pressed_pointer_buttons = mulan::engine::MouseButton::None;
    impl_->attachment.reset();
    impl_->session = nullptr;
    impl_->view_context.shutdown();
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

    rebuildAttachment();
    LOG_INFO("[Editor] Document view initialized: name={}, size={}x{}",
             impl_->session ? std::string_view(impl_->session->displayName()) : std::string_view("<unbound>"), width,
             height);
    invalidateFrame();
    return true;
}

void DocumentView::resize(int width, int height) {
    if (impl_->view_context.isReady()) {
        impl_->view_context.resize(width, height);
        if (impl_->attachment) {
            impl_->attachment->editorSession().refreshGrips();
        }
        invalidateFrame();
    }
}

mulan::ResultVoid DocumentView::renderFrame() {
    const bool interactionActive = (impl_->attachment && impl_->attachment->editorSession().hasActiveTool()) ||
                                   impl_->view_context.isCameraNavigating();
    if (impl_->attachment) {
        impl_->attachment->binding().prepareFrame(interactionActive ? ClipUpdateMode::Interactive
                                                                    : ClipUpdateMode::Settled);
    }
    return impl_->view_context.renderFrame();
}

mulan::ResultVoid DocumentView::consumeRenderEvents() {
    return impl_->view_context.consumeRenderEvents();
}

void DocumentView::fitAll() {
    if (!impl_->view_context.isReady()) {
        return;
    }

    if (impl_->attachment) {
        impl_->attachment->binding().fitAll();
        impl_->attachment->editorSession().refreshGrips();
    }
}

void DocumentView::setCameraToWorldXY() {
    if (!impl_->view_context.isReady()) {
        return;
    }

    impl_->view_context.setCameraToWorldXY();
    if (impl_->attachment) {
        impl_->attachment->editorSession().refreshGrips();
    }
    invalidateFrame();
}

mulan::engine::ProjectionMode DocumentView::projectionMode() const {
    return impl_->view_context.camera().projectionMode();
}

void DocumentView::setProjectionMode(mulan::engine::ProjectionMode mode) {
    auto& camera = impl_->view_context.camera();
    if (camera.projectionMode() == mode) {
        return;
    }

    camera.setProjectionMode(mode);
    if (impl_->attachment) {
        impl_->attachment->editorSession().refreshGrips();
    }
    invalidateFrame();
}

void DocumentView::setFrameInvalidationCallback(std::function<void()> callback) {
    impl_->frame_invalidation_callback = std::move(callback);
}

bool DocumentView::isReady() const {
    return impl_->view_context.isReady();
}

mulan::view::RenderMode DocumentView::renderMode() const {
    return impl_->view_context.renderMode();
}

void DocumentView::setRenderMode(mulan::view::RenderMode mode) {
    if (impl_->view_context.renderMode() == mode) {
        return;
    }
    impl_->view_context.setRenderMode(mode);
    invalidateFrame();
}

bool DocumentView::viewCubeVisible() const {
    return impl_->view_context.showViewCube();
}

void DocumentView::setViewCubeVisible(bool visible) {
    if (impl_->view_context.showViewCube() == visible) {
        return;
    }
    impl_->view_context.setShowViewCube(visible);
    invalidateFrame();
}

mulan::Result<mulan::view::CaptureImage> DocumentView::capture(mulan::view::CaptureRequest request) {
    if (!isReady()) {
        return std::unexpected(mulan::Error::make(mulan::ErrorCode::InvalidArg, "Document view is not ready"));
    }
    request.camera = impl_->view_context.camera();
    return impl_->view_context.capture(request);
}

mulan::engine::WorkPlane DocumentView::viewWorkPlane() const {
    return mulan::engine::WorkPlane::fromView(impl_->view_context.camera());
}

DocumentSession* DocumentView::session() const {
    return impl_->session;
}

CommandHost DocumentView::commandHost() {
    return CommandHost(this, impl_->attachment ? &impl_->attachment->editorSession() : nullptr);
}

void DocumentView::setDocumentSession(DocumentSession* session) {
    MULAN_PROFILE_ZONE();

    clearClickTracking();
    impl_->pressed_pointer_buttons = mulan::engine::MouseButton::None;
    impl_->attachment.reset();
    impl_->session = session;
    LOG_DEBUG("[Editor] Document view session changed: name={}",
              impl_->session ? std::string_view(impl_->session->displayName()) : std::string_view("<none>"));

    rebuildAttachment();
    invalidateFrame();
}

DocumentInputOutcome DocumentView::handleInput(const mulan::engine::InputEvent& event) {
    DocumentInputOutcome result;
    EditorSession* editor = impl_->attachment ? &impl_->attachment->editorSession() : nullptr;
    const bool hadActiveTool = editor && editor->hasActiveTool();

    // 取消事件优先：统一通知 editor 与 view 两端清理临时交互。
    if (event.isCancelEvent()) {
        impl_->pressed_pointer_buttons = mulan::engine::MouseButton::None;
        clearClickTracking();

        // 先让栈顶 Operator 在自身调用栈内完成“标记结束”，ViewContext 会在返回后
        // 安全弹栈。若工具与 Operator 状态意外失配，再由 Session 做幂等兜底清理。
        impl_->view_context.dispatchInput(event);
        if (editor && editor->hasActiveTool()) {
            editor->cancelActiveTool();
        }
        if (editor) {
            editor->clearHover();
        }

        result.disposition = DocumentInputDisposition::Cancelled;
        result.frameInvalidated = true;
        result.commandStateInvalidated = hadActiveTool;
        return result;
    }

    trackPointerButtons(event);

    // 跟踪左键 press，用于 release 时的 click-vs-drag 选择判定。
    // 活动工具拥有自己的左键事务，不应创建文档选择 click tracker。
    if (!hadActiveTool) {
        trackPressEvent(event);
    }

    const bool editorInteractionStarted = editor && editor->handleInput(event);
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
            if (editor) {
                editor->refreshGrips();
            }
        }

        const bool toolEnded = hadActiveTool && editor && !editor->hasActiveTool();
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
        if (editor) {
            if (impl_->view_context.hasHoveredViewCubeFace() || editor->hasActiveTool()) {
                editor->clearHover();
            } else {
                editor->updateHoverAtFramebuffer(static_cast<double>(event.x), static_cast<double>(event.y));
            }
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

void DocumentView::trackPointerButtons(const mulan::engine::InputEvent& event) {
    if (event.isMouseEvent()) {
        // 只记录进入本视口输入边界的按钮状态。不能查询平台全局按钮：
        // 点击 Ribbon 导致失焦时，全局左键按下并不代表视口仍有未完成事务。
        impl_->pressed_pointer_buttons = event.buttons;
    }
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
    if (impl_->attachment) {
        impl_->attachment->editorSession().updateHoverAtFramebuffer(x, y);
    }
}

void DocumentView::selectAtFramebuffer(double x, double y) {
    if (impl_->attachment) {
        impl_->attachment->editorSession().selectAtFramebuffer(x, y);
    }
}

DocumentInputOutcome DocumentView::cancelInteraction() {
    // FocusLost 统一走 handleInput 的生命周期路径：工具、camera delegate、默认相机、
    // ViewCube click 事务与文档 click tracker 在同一个边界内清理。
    return handleInput(mulan::engine::InputEvent::focusLost());
}

DocumentInputOutcome DocumentView::handleFocusLost() {
    if (impl_->pressed_pointer_buttons != mulan::engine::MouseButton::None) {
        return cancelInteraction();
    }
    return clearTransientInteraction();
}

DocumentInputOutcome DocumentView::clearTransientInteraction() {
    clearClickTracking();
    impl_->view_context.clearViewCubeHover();
    if (impl_->attachment) {
        impl_->attachment->editorSession().clearHover();
        impl_->attachment->editorSession().clearSnapPreview();
    }

    DocumentInputOutcome result;
    result.disposition = DocumentInputDisposition::Ignored;
    result.frameInvalidated = true;
    return result;
}

void DocumentView::invalidateFrame() const {
    if (impl_->frame_invalidation_callback) {
        impl_->frame_invalidation_callback();
    }
}

void DocumentView::rebuildAttachment() {
    impl_->attachment.reset();
    if (!impl_->session || !impl_->view_context.isReady()) {
        return;
    }
    impl_->attachment = std::make_unique<DocumentViewAttachment>(*impl_->session, impl_->view_context,
                                                                 [this]() { invalidateFrame(); });
}

void DocumentView::onCommandCompleted() {
    invalidateFrame();
}

}  // namespace mulan::editor
