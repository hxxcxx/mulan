#include "doc_widget.h"

#include "platform/qt_native_window_handle.h"
#include "qt_viewport_input_adapter.h"
#include "engine_settings.h"

#include <mulan/core/log/log.h>
#include <mulan/view/core/view_context.h>

#include <QMetaObject>
#include <QShowEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>

DocWidget::DocWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);

    // Document/View 深层只报告失效，真正提交统一回到本 Qt 所有者。
    document_view_.setFrameInvalidationCallback([this]() { requestFrame(); });
}

DocWidget::~DocWidget() {
    // 先停止通道，保证 RenderThread 不再向 runtime_event_proxy_ 投递事件。
    document_view_.setFrameInvalidationCallback({});
    shutdown();
}

bool DocWidget::init() {
    if (document_view_.isInitialized()) {
        runtime_state_ = RuntimeState::Ready;
        requestFrame();
        return true;
    }

    runtime_state_ = RuntimeState::Starting;

    EngineSettings::instance().applyTo(view_config_);
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

void DocWidget::shutdown() {
    if (runtime_state_ == RuntimeState::Stopping) {
        return;
    }

    runtime_state_ = RuntimeState::Stopping;
    frame_invalidated_ = false;
    clearPreview(false);
    document_view_.setDocumentSession(nullptr);
    document_view_.viewContext().shutdown();
    runtime_state_ = RuntimeState::Stopped;
}

void DocWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (document_view_.isInitialized()) {
        const qreal dpr = devicePixelRatioF();
        input_adapter_.setDevicePixelRatioF(dpr);
        const int pw = static_cast<int>(width() * dpr);
        const int ph = static_cast<int>(height() * dpr);
        document_view_.resize(pw, ph);
    }
}

void DocWidget::paintEvent(QPaintEvent*) {
    if (runtime_state_ == RuntimeState::Ready && document_view_.isInitialized()) {
        // update() 会合并同一轮事件循环中的失效；系统 expose 也必须重绘原生 Surface。
        frame_invalidated_ = true;
        submitPendingFrame();
    }
}

void DocWidget::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (frame_invalidated_) {
        update();
    }
}

void DocWidget::mousePressEvent(QMouseEvent* e) {
    applyResult(document_view_.handleInput(input_adapter_.mousePress(*e)));
}

void DocWidget::mouseReleaseEvent(QMouseEvent* e) {
    applyResult(document_view_.handleInput(input_adapter_.mouseRelease(*e)));
}

void DocWidget::mouseMoveEvent(QMouseEvent* e) {
    applyResult(document_view_.handleInput(input_adapter_.mouseMove(*e)));
}

void DocWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    applyResult(document_view_.handleInput(input_adapter_.mouseDoubleClick(*e)));
}

void DocWidget::wheelEvent(QWheelEvent* e) {
    applyResult(document_view_.handleInput(input_adapter_.wheel(*e)));
}

void DocWidget::keyPressEvent(QKeyEvent* e) {
    applyResult(document_view_.handleInput(input_adapter_.keyPress(*e)));
}

void DocWidget::keyReleaseEvent(QKeyEvent* e) {
    applyResult(document_view_.handleInput(input_adapter_.keyRelease(*e)));
}

void DocWidget::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    // 离开视口：所有临时状态在 DocumentView 的统一取消边界内清理。
    applyResult(document_view_.cancelInteraction());
}

void DocWidget::focusOutEvent(QFocusEvent* e) {
    applyResult(document_view_.cancelInteraction());
    QWidget::focusOutEvent(e);
}

bool DocWidget::event(QEvent* e) {
    switch (e->type()) {
    case QEvent::WindowDeactivate:
    case QEvent::UngrabMouse: applyResult(document_view_.cancelInteraction()); break;
    default: break;
    }
    return QWidget::event(e);
}

void DocWidget::setDocumentSession(DocumentSession* session) {
    document_view_.setDocumentSession(session);
    if (document_view_.isInitialized() && session) {
        requestFrame();
    }
}

void DocWidget::requestFrame() {
    if (runtime_state_ != RuntimeState::Ready) {
        return;
    }

    frame_invalidated_ = true;
    update();
}

void DocWidget::queueRuntimeEvent() {
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

void DocWidget::submitPendingFrame() {
    if (runtime_state_ != RuntimeState::Ready || !frame_invalidated_) {
        return;
    }
    auto runtime = document_view_.pollRenderRuntime();
    if (!runtime) {
        transitionToRuntimeFailure(QString::fromStdString(runtime.error().message));
        return;
    }
    if (!document_view_.isInitialized()) {
        transitionToRuntimeFailure(tr("The render channel stopped unexpectedly."));
        return;
    }
    if (!isVisible()) {
        // 隐藏期间保留失效位，下一次 show/paint 再提交。
        return;
    }

    frame_invalidated_ = false;
    document_view_.renderFrame();
    runtime = document_view_.pollRenderRuntime();
    if (!runtime) {
        transitionToRuntimeFailure(QString::fromStdString(runtime.error().message));
        return;
    }
    if (!document_view_.isInitialized()) {
        transitionToRuntimeFailure(tr("The render channel rejected the frame and stopped."));
    }
}

void DocWidget::consumeRuntimeEvent() {
    if (runtime_state_ != RuntimeState::Ready) {
        return;
    }

    // RenderThread 只在资源 ACK 或 failure 发布时唤醒 owner；这里统一消费状态。
    auto runtime = document_view_.pollRenderRuntime();
    if (!runtime) {
        transitionToRuntimeFailure(QString::fromStdString(runtime.error().message));
        return;
    }
    if (!document_view_.isInitialized()) {
        transitionToRuntimeFailure(tr("The render channel stopped unexpectedly."));
    }
}

void DocWidget::transitionToRuntimeFailure(QString message) {
    if (runtime_state_ == RuntimeState::Failed || runtime_state_ == RuntimeState::Stopping) {
        return;
    }

    frame_invalidated_ = false;
    runtime_state_ = RuntimeState::Failed;
    LOG_ERROR("[App] Document render channel stopped: {}", message.toStdString());
}

void DocWidget::applyResult(const DocumentInputOutcome& result) {
    if (result.needsFrame()) {
        requestFrame();
    }
    if (result.needsCommandStateRefresh()) {
        emit commandStateInvalidated();
    }
}

void DocWidget::clearPreview(bool refresh) {
    document_view_.viewContext().clearPreview();
    if (refresh) {
        requestFrame();
    }
}
