#include "doc_widget.h"

#include "qt_viewport_input_adapter.h"
#include "engine_settings.h"

#include <mulan/view/core/view_context.h>

#include <QShowEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>

#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <QApplication>
#include <QtGui/qguiapplication_platform.h>
#endif

DocWidget::DocWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);

    // Document/View 深层只报告失效，真正提交统一回到本 Qt 所有者。
    document_view_.setFrameInvalidationCallback([this]() { requestFrame(); });

    frame_dispatch_timer_.setSingleShot(true);
    frame_dispatch_timer_.setInterval(0);
    frame_dispatch_timer_.setTimerType(Qt::PreciseTimer);
    connect(&frame_dispatch_timer_, &QTimer::timeout, this, &DocWidget::submitPendingFrame);

    runtime_health_timer_.setInterval(250);
    runtime_health_timer_.setTimerType(Qt::CoarseTimer);
    connect(&runtime_health_timer_, &QTimer::timeout, this, &DocWidget::checkRuntimeHealth);
}

DocWidget::~DocWidget() {
    // QTimer 成员先于 DocumentView 析构；提前断开反向回调，避免析构期访问已销毁调度状态。
    document_view_.setFrameInvalidationCallback({});
    shutdown();
}

bool DocWidget::init() {
    if (document_view_.isInitialized()) {
        runtime_state_ = RuntimeState::Ready;
        runtime_health_timer_.start();
        requestFrame();
        return true;
    }

    runtime_state_ = RuntimeState::Starting;
    runtime_failure_message_.clear();

    EngineSettings::instance().applyTo(view_config_);

#ifdef _WIN32
    view_config_.hInstance = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    view_config_.hWnd = reinterpret_cast<uintptr_t>(HWND(winId()));
#else
    if (auto* x11 = qApp->nativeInterface<QNativeInterface::QX11Application>()) {
        view_config_.displayConnection = reinterpret_cast<uintptr_t>(x11->connection());
        view_config_.windowHandle = static_cast<uintptr_t>(winId());
    }
#endif

    const qreal dpr = devicePixelRatioF();
    input_adapter_.setDevicePixelRatioF(dpr);
    const int pw = static_cast<int>(width() * dpr);
    const int ph = static_cast<int>(height() * dpr);
    if (!document_view_.init(view_config_, pw, ph)) {
        transitionToRuntimeFailure(tr("Failed to initialize the dedicated render thread."));
        return false;
    }

    runtime_state_ = RuntimeState::Ready;
    runtime_health_timer_.start();
    requestFrame();
    return true;
}

void DocWidget::shutdown() {
    if (runtime_state_ == RuntimeState::Stopping) {
        return;
    }

    runtime_state_ = RuntimeState::Stopping;
    frame_dispatch_timer_.stop();
    runtime_health_timer_.stop();
    frame_invalidated_ = false;
    clearPreview(false);
    document_view_.setDocumentSession(nullptr);
    document_view_.viewContext().shutdown();
    runtime_failure_message_.clear();
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
    if (document_view_.isInitialized())
        requestFrame();
}

void DocWidget::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    schedulePendingFrame();
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
    schedulePendingFrame();
}

void DocWidget::schedulePendingFrame() {
    if (runtime_state_ == RuntimeState::Ready && frame_invalidated_ && document_view_.isInitialized() && isVisible() &&
        !frame_dispatch_timer_.isActive()) {
        frame_dispatch_timer_.start();
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
        transitionToRuntimeFailure(tr("The dedicated render thread stopped unexpectedly."));
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
        transitionToRuntimeFailure(tr("The dedicated render thread rejected the frame and stopped."));
    }
}

void DocWidget::checkRuntimeHealth() {
    if (runtime_state_ != RuntimeState::Ready) {
        return;
    }

    // 即使画面稳定且没有新帧，也必须持续泵出资源 ACK；否则 builder 会长期
    // 保留整批 CPU 网格/图像快照。Failure 也在同一 owner 边界完成销毁与失效。
    auto runtime = document_view_.pollRenderRuntime();
    if (!runtime) {
        transitionToRuntimeFailure(QString::fromStdString(runtime.error().message));
        return;
    }
    if (!document_view_.isInitialized()) {
        transitionToRuntimeFailure(tr("The dedicated render thread stopped unexpectedly."));
    }
}

void DocWidget::transitionToRuntimeFailure(QString message) {
    if (runtime_state_ == RuntimeState::Failed || runtime_state_ == RuntimeState::Stopping) {
        return;
    }

    frame_dispatch_timer_.stop();
    runtime_health_timer_.stop();
    frame_invalidated_ = false;
    runtime_failure_message_ = std::move(message);
    runtime_state_ = RuntimeState::Failed;
    emit renderRuntimeFailed(runtime_failure_message_);
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
