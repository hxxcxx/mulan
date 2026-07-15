#include "doc_widget.h"

#include "qt_viewport_input_adapter.h"
#include "engine_settings.h"

#include <QShowEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>

#ifdef _WIN32
#include <windows.h>
#endif

DocWidget::DocWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
}

DocWidget::~DocWidget() {
    shutdown();
}

bool DocWidget::init() {
    if (document_view_.isInitialized())
        return true;

    EngineSettings::instance().applyTo(view_config_);

#ifdef _WIN32
    view_config_.hInstance = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    view_config_.hWnd = reinterpret_cast<uintptr_t>(HWND(winId()));
#endif

    const qreal dpr = devicePixelRatioF();
    input_adapter_.setDevicePixelRatioF(dpr);
    const int pw = static_cast<int>(width() * dpr);
    const int ph = static_cast<int>(height() * dpr);
    return document_view_.init(view_config_, pw, ph);
}

void DocWidget::shutdown() {
    clearPreview(false);
    document_view_.setDocumentSession(nullptr);
    document_view_.viewContext().shutdown();
}

void DocWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (document_view_.isInitialized()) {
        const qreal dpr = devicePixelRatioF();
        input_adapter_.setDevicePixelRatioF(dpr);
        const int pw = static_cast<int>(width() * dpr);
        const int ph = static_cast<int>(height() * dpr);
        document_view_.resize(pw, ph);
        requestFrame();
    }
}

void DocWidget::paintEvent(QPaintEvent*) {
    if (document_view_.isInitialized())
        requestFrame();
}

void DocWidget::mousePressEvent(QMouseEvent* e) {
    const bool consumed = document_view_.handleInput(input_adapter_.mousePress(*e));
    applyResult(consumed);
}

void DocWidget::mouseReleaseEvent(QMouseEvent* e) {
    const bool consumed = document_view_.handleInput(input_adapter_.mouseRelease(*e));
    applyResult(consumed);
}

void DocWidget::mouseMoveEvent(QMouseEvent* e) {
    const bool consumed = document_view_.handleInput(input_adapter_.mouseMove(*e));

    // 无按钮按下且事件未消费时更新 hover（ViewCube hover 优先排除）。
    if (e->buttons() == Qt::NoButton && !consumed) {
        if (!document_view_.viewContext().hasHoveredViewCubeFace()) {
            updateHoverAtFramebuffer(input_adapter_.framebufferPosition(e->pos()));
        } else {
            document_view_.clearEditorHover();
        }
    }
    requestFrame();
}

void DocWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    const bool consumed = document_view_.handleInput(input_adapter_.mouseDoubleClick(*e));
    applyResult(consumed);
}

void DocWidget::wheelEvent(QWheelEvent* e) {
    document_view_.handleInput(input_adapter_.wheel(*e));
    requestFrame();
}

void DocWidget::keyPressEvent(QKeyEvent* e) {
    const bool consumed = document_view_.handleInput(input_adapter_.keyPress(*e));
    applyResult(consumed);
}

void DocWidget::keyReleaseEvent(QKeyEvent* e) {
    const bool consumed = document_view_.handleInput(input_adapter_.keyRelease(*e));
    applyResult(consumed);
}

void DocWidget::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    // 离开视口：取消临时交互 + 清理 hover（统一入口，不再分散调用子系统）。
    document_view_.cancelInteraction();
    document_view_.clearEditorHover();
    document_view_.viewContext().clearHoveredPickId();
    requestFrame();
}

void DocWidget::focusOutEvent(QFocusEvent* e) {
    document_view_.cancelInteraction();
    QWidget::focusOutEvent(e);
}

bool DocWidget::event(QEvent* e) {
    switch (e->type()) {
    case QEvent::WindowDeactivate:
    case QEvent::UngrabMouse:
        document_view_.cancelInteraction();
        break;
    default:
        break;
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
    if (document_view_.isInitialized() && isVisible()) {
        document_view_.renderFrame();
    }
}

void DocWidget::applyResult(bool consumed) {
    requestFrame();
    // 仅在事件确实改变编辑状态时刷新命令 UI，而非每个事件无条件刷新。
    // 活动工具存在或事件被消费时，可能改变工具/选择/历史状态。
    if (consumed || document_view_.hasActiveEditorTool()) {
        emit commandStateInvalidated();
    }
}

void DocWidget::updateHoverAtFramebuffer(const QPointF& framebufferPos) {
    document_view_.updateHoverAtFramebuffer(framebufferPos.x(), framebufferPos.y());
}

void DocWidget::clearPreview(bool refresh) {
    document_view_.viewContext().clearPreview();
    if (refresh) {
        requestFrame();
    }
}
