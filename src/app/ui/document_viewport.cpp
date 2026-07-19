#include "document_viewport.h"

#include "../platform/qt_native_window_handle.h"
#include "qt_viewport_input_adapter.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/view/core/view_context.h>

#include <QMetaObject>
#include <QShowEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <utility>
#include <QKeyEvent>
#include <QFocusEvent>

DocumentViewport::DocumentViewport(const mulan::view::ViewConfig& viewConfig, QWidget* parent)
    : QWidget(parent), view_config_(viewConfig) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);

    // Document/View 深层只报告失效，真正提交统一回到本 Qt 所有者。
    document_view_.setFrameInvalidationCallback([this]() { requestFrame(); });
}

DocumentViewport::~DocumentViewport() {
    // 先停止通道，保证 RenderThread 不再向 runtime_event_proxy_ 投递事件。
    document_view_.setFrameInvalidationCallback({});
    shutdown();
}

bool DocumentViewport::init() {
    MULAN_PROFILE_ZONE();

    if (document_view_.isReady()) {
        runtime_state_ = RuntimeState::Ready;
        requestFrame();
        return true;
    }

    runtime_state_ = RuntimeState::Starting;

    view_config_.window = mulan::app::nativeWindowHandle(*this);

    const qreal dpr = devicePixelRatioF();
    input_adapter_.setDevicePixelRatioF(dpr);
    const int pw = static_cast<int>(width() * dpr);
    const int ph = static_cast<int>(height() * dpr);
    if (!document_view_.init(view_config_, pw, ph, [this]() { queueRuntimeEvent(); })) {
        transitionToRuntimeFailure(tr("Failed to initialize the render channel."));
        return false;
    }

    runtime_state_ = RuntimeState::Ready;
    requestFrame();
    return true;
}

void DocumentViewport::shutdown() {
    if (runtime_state_ == RuntimeState::Stopping) {
        return;
    }

    runtime_state_ = RuntimeState::Stopping;
    frame_invalidated_ = false;
    document_view_.shutdown();
    runtime_state_ = RuntimeState::Stopped;
}

void DocumentViewport::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (document_view_.isReady()) {
        const qreal dpr = devicePixelRatioF();
        input_adapter_.setDevicePixelRatioF(dpr);
        const int pw = static_cast<int>(width() * dpr);
        const int ph = static_cast<int>(height() * dpr);
        document_view_.resize(pw, ph);
    }
}

void DocumentViewport::paintEvent(QPaintEvent*) {
    if (runtime_state_ == RuntimeState::Ready && document_view_.isReady()) {
        // update() 会合并同一轮事件循环中的失效；系统 expose 也必须重绘原生 Surface。
        frame_invalidated_ = true;
        submitPendingFrame();
    }
}

void DocumentViewport::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (frame_invalidated_) {
        update();
    }
}

void DocumentViewport::mousePressEvent(QMouseEvent* e) {
    if (!isReady()) {
        e->ignore();
        return;
    }
    applyResult(document_view_.handleInput(input_adapter_.mousePress(*e)));
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* e) {
    if (!isReady()) {
        e->ignore();
        return;
    }
    applyResult(document_view_.handleInput(input_adapter_.mouseRelease(*e)));
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* e) {
    if (!isReady()) {
        e->ignore();
        return;
    }
    applyResult(document_view_.handleInput(input_adapter_.mouseMove(*e)));
}

void DocumentViewport::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!isReady()) {
        e->ignore();
        return;
    }
    applyResult(document_view_.handleInput(input_adapter_.mouseDoubleClick(*e)));
}

void DocumentViewport::wheelEvent(QWheelEvent* e) {
    if (!isReady()) {
        e->ignore();
        return;
    }
    applyResult(document_view_.handleInput(input_adapter_.wheel(*e)));
}

void DocumentViewport::keyPressEvent(QKeyEvent* e) {
    if (!isReady()) {
        e->ignore();
        return;
    }
    applyResult(document_view_.handleInput(input_adapter_.keyPress(*e)));
}

void DocumentViewport::keyReleaseEvent(QKeyEvent* e) {
    if (!isReady()) {
        e->ignore();
        return;
    }
    applyResult(document_view_.handleInput(input_adapter_.keyRelease(*e)));
}

void DocumentViewport::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    // 离开视口：所有临时状态在 DocumentView 的统一取消边界内清理。
    if (isReady()) {
        applyResult(document_view_.cancelInteraction());
    }
}

void DocumentViewport::focusOutEvent(QFocusEvent* e) {
    if (isReady()) {
        applyResult(document_view_.cancelInteraction());
    }
    QWidget::focusOutEvent(e);
}

bool DocumentViewport::event(QEvent* e) {
    switch (e->type()) {
    case QEvent::WindowDeactivate:
    case QEvent::UngrabMouse:
        if (isReady()) {
            applyResult(document_view_.cancelInteraction());
        }
        break;
    default: break;
    }
    return QWidget::event(e);
}

void DocumentViewport::setDocumentSession(mulan::editor::DocumentSession* session) {
    document_view_.setDocumentSession(session);
    if (document_view_.isReady() && session) {
        requestFrame();
    }
}

bool DocumentViewport::isReady() const {
    return runtime_state_ == RuntimeState::Ready && document_view_.isReady();
}

mulan::editor::CommandHost DocumentViewport::commandHost() {
    return isReady() ? document_view_.commandHost() : mulan::editor::CommandHost{};
}

mulan::view::RenderMode DocumentViewport::renderMode() const {
    return document_view_.renderMode();
}

void DocumentViewport::setRenderMode(mulan::view::RenderMode mode) {
    if (isReady()) {
        document_view_.setRenderMode(mode);
    }
}

mulan::view::SurfaceShading DocumentViewport::surfaceShading() const {
    return document_view_.surfaceShading();
}

void DocumentViewport::setSurfaceShading(mulan::view::SurfaceShading shading) {
    if (isReady()) {
        document_view_.setSurfaceShading(shading);
    }
}

bool DocumentViewport::viewCubeVisible() const {
    return document_view_.viewCubeVisible();
}

void DocumentViewport::setViewCubeVisible(bool visible) {
    if (isReady()) {
        document_view_.setViewCubeVisible(visible);
    }
}

mulan::Result<mulan::view::CaptureImage> DocumentViewport::capture(mulan::view::CaptureRequest request) {
    return document_view_.capture(std::move(request));
}

void DocumentViewport::requestFrame() {
    if (runtime_state_ != RuntimeState::Ready) {
        return;
    }

    frame_invalidated_ = true;
    update();
}

void DocumentViewport::queueRuntimeEvent() {
    bool expected = false;
    if (!runtime_event_pending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    const bool queued = QMetaObject::invokeMethod(
            &runtime_event_proxy_,
            [this]() {
                runtime_event_pending_.store(false, std::memory_order_release);
                consumeRuntimeEvent();
            },
            Qt::QueuedConnection);
    if (!queued) {
        runtime_event_pending_.store(false, std::memory_order_release);
    }
}

void DocumentViewport::submitPendingFrame() {
    if (runtime_state_ != RuntimeState::Ready || !frame_invalidated_) {
        return;
    }
    if (!document_view_.isReady()) {
        transitionToRuntimeFailure(tr("The render channel stopped unexpectedly."));
        return;
    }
    if (!isVisible()) {
        // 隐藏期间保留失效位，下一次 show/paint 再提交。
        return;
    }

    frame_invalidated_ = false;
    auto submitted = document_view_.renderFrame();
    if (!submitted) {
        transitionToRuntimeFailure(QString::fromStdString(submitted.error().message));
    }
}

void DocumentViewport::consumeRuntimeEvent() {
    if (runtime_state_ != RuntimeState::Ready) {
        return;
    }

    // RenderThread 只在资源 ACK 或 failure 发布时唤醒 owner；这里统一消费状态。
    auto runtime = document_view_.consumeRenderEvents();
    if (!runtime) {
        transitionToRuntimeFailure(QString::fromStdString(runtime.error().message));
        return;
    }
    if (!document_view_.isReady()) {
        transitionToRuntimeFailure(tr("The render channel stopped unexpectedly."));
    }
}

void DocumentViewport::transitionToRuntimeFailure(QString message) {
    if (runtime_state_ == RuntimeState::Failed || runtime_state_ == RuntimeState::Stopping) {
        return;
    }

    frame_invalidated_ = false;
    runtime_state_ = RuntimeState::Failed;
    LOG_ERROR("[App] Document render channel stopped: {}", message.toStdString());
    emit commandStateInvalidated();
    emit runtimeFailed(message);
}

void DocumentViewport::applyResult(const mulan::editor::DocumentInputOutcome& result) {
    if (result.needsFrame()) {
        requestFrame();
    }
    if (result.needsCommandStateRefresh()) {
        emit commandStateInvalidated();
    }
}
