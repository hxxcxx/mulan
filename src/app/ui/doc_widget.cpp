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
    if (document_view_.isInitialized() && isVisible()) {
        document_view_.renderFrame();
    }
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
