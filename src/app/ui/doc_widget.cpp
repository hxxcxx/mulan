#include "doc_widget.h"

#include "document/document_session.h"
#include "engine_settings.h"

#include <QShowEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace mulan::engine;

DocWidget::DocWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
}

DocWidget::~DocWidget() {
    clearPreview(false);
}

void DocWidget::init() {
    if (document_view_.isInitialized())
        return;

    // 从全局设置读取后端 / MSAA / VSync 等配置。
    EngineSettings::instance().applyTo(view_config_);

    // 填充平台原生窗口信息。
#ifdef _WIN32
    view_config_.hInstance = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    view_config_.hWnd = reinterpret_cast<uintptr_t>(HWND(winId()));
#endif

    const qreal dpr = devicePixelRatioF();
    const int pw = static_cast<int>(width() * dpr);
    const int ph = static_cast<int>(height() * dpr);
    document_view_.init(view_config_, pw, ph);
}

void DocWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (document_view_.isInitialized()) {
        const qreal dpr = devicePixelRatioF();
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
    if (e->button() == Qt::LeftButton) {
        press_pos_ = e->pos();
        left_press_pending_ = true;
        left_press_dragged_ = false;
        left_press_consumed_ = false;
    }

    const bool hadEditorTool = document_view_.hasActiveEditorTool();
    auto ev = makeMousePressEvent(*e);
    const bool consumed = document_view_.handleInput(ev);
    if (e->button() == Qt::LeftButton) {
        left_press_consumed_ = consumed && (hadEditorTool || document_view_.hasActiveEditorTool());
    }
    requestFrame();
    emit commandStateInvalidated();
}

void DocWidget::mouseReleaseEvent(QMouseEvent* e) {
    const bool hadEditorTool = document_view_.hasActiveEditorTool();
    auto ev = makeMouseReleaseEvent(*e);
    const bool consumed = document_view_.handleInput(ev);

    if (e->button() == Qt::LeftButton && left_press_pending_) {
        if (consumed && hadEditorTool) {
            left_press_consumed_ = true;
        }
        if (!left_press_dragged_ && !left_press_consumed_ && !document_view_.viewContext().hasHoveredViewCubeFace()) {
            selectAtFramebuffer(framebufferPosition(e->pos()));
        }
        left_press_pending_ = false;
        left_press_dragged_ = false;
        left_press_consumed_ = false;
    }
    requestFrame();
    emit commandStateInvalidated();
}

void DocWidget::mouseMoveEvent(QMouseEvent* e) {
    if (left_press_pending_ && (e->pos() - press_pos_).manhattanLength() > 4) {
        left_press_dragged_ = true;
    }

    auto ev = makeMouseMoveEvent(*e);
    const bool consumed = document_view_.handleInput(ev);

    if (e->buttons() == Qt::NoButton && !consumed) {
        if (!document_view_.viewContext().hasHoveredViewCubeFace()) {
            updateHoverAtFramebuffer(framebufferPosition(e->pos()));
        } else {
            document_view_.clearEditorHover();
        }
    }
    requestFrame();
}

void DocWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    auto ev = makeMouseDoubleClickEvent(*e);
    document_view_.handleInput(ev);
    requestFrame();
    emit commandStateInvalidated();
}

void DocWidget::wheelEvent(QWheelEvent* e) {
    auto ev = makeWheelEvent(*e);
    document_view_.handleInput(ev);
    requestFrame();
}

void DocWidget::keyPressEvent(QKeyEvent* e) {
    auto ev = InputEvent::keyPress(translateKey(e->key()), translateModifiers(e->modifiers()));
    document_view_.handleInput(ev);
    requestFrame();
    emit commandStateInvalidated();
}

void DocWidget::keyReleaseEvent(QKeyEvent* e) {
    auto ev = InputEvent::keyRelease(translateKey(e->key()), translateModifiers(e->modifiers()));
    document_view_.handleInput(ev);
    requestFrame();
    emit commandStateInvalidated();
}

void DocWidget::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    document_view_.clearEditorHover();
    document_view_.viewContext().clearHoveredPickId();
    document_view_.viewContext().clearViewCubeInteraction();
    requestFrame();
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

QPoint DocWidget::framebufferEventPosition(const QPointF& pos) const {
    // Qt 鼠标事件给的是 widget logical coordinates；RHI swapchain、Camera::screenRay()
    // 和 ViewCubeModel::pickFace() 使用的是 framebuffer coordinates。
    // 高 DPI 屏幕下两者不同，必须在进入 view/engine picking 前乘 devicePixelRatioF()。
    //
    // 注意：click-vs-drag 阈值仍然使用原始 Qt logical coordinates（press_pos_ / e->pos()），
    // 事件路径使用四舍五入后的 framebuffer 坐标；ray picking 另走浮点坐标以避免 DPR 截断误差。
    const qreal dpr = devicePixelRatioF();
    return QPoint(qRound(pos.x() * dpr), qRound(pos.y() * dpr));
}

QPointF DocWidget::framebufferPosition(const QPointF& pos) const {
    const qreal dpr = devicePixelRatioF();
    return QPointF(pos.x() * dpr, pos.y() * dpr);
}

InputEvent DocWidget::makeMousePressEvent(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    return InputEvent::mousePress(p.x(), p.y(), translateButton(e.button()), translateButtons(e.buttons()),
                                  translateModifiers(e.modifiers()));
}

InputEvent DocWidget::makeMouseReleaseEvent(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    return InputEvent::mouseRelease(p.x(), p.y(), translateButton(e.button()), translateButtons(e.buttons()),
                                    translateModifiers(e.modifiers()));
}

InputEvent DocWidget::makeMouseMoveEvent(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    return InputEvent::mouseMove(p.x(), p.y(), translateButtons(e.buttons()), translateModifiers(e.modifiers()));
}

InputEvent DocWidget::makeMouseDoubleClickEvent(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    InputEvent ev{};
    ev.type = InputEvent::Type::MouseDoubleClick;
    ev.x = p.x();
    ev.y = p.y();
    ev.button = translateButton(e.button());
    ev.buttons = translateButtons(e.buttons());
    ev.modifiers = translateModifiers(e.modifiers());
    return ev;
}

InputEvent DocWidget::makeWheelEvent(const QWheelEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    const QPoint pixelDelta = e.pixelDelta();
    const float delta = !pixelDelta.isNull() ? static_cast<float>(pixelDelta.y()) / 120.0f
                                             : static_cast<float>(e.angleDelta().y()) / 120.0f;
    return InputEvent::wheel(p.x(), p.y(), delta, translateModifiers(e.modifiers()));
}

void DocWidget::updateHoverAtFramebuffer(const QPointF& framebufferPos) {
    document_view_.updateHoverAtFramebuffer(framebufferPos.x(), framebufferPos.y());
}

void DocWidget::selectAtFramebuffer(const QPointF& framebufferPos) {
    document_view_.selectAtFramebuffer(framebufferPos.x(), framebufferPos.y());
}

void DocWidget::clearPreview(bool refresh) {
    document_view_.viewContext().clearPreview();
    if (refresh) {
        requestFrame();
    }
}

mulan::engine::MouseButton DocWidget::translateButton(Qt::MouseButton btn) {
    switch (btn) {
    case Qt::LeftButton: return MouseButton::Left;
    case Qt::RightButton: return MouseButton::Right;
    case Qt::MiddleButton: return MouseButton::Middle;
    default: return MouseButton::None;
    }
}

mulan::engine::MouseButton DocWidget::translateButtons(Qt::MouseButtons btns) {
    MouseButton result = MouseButton::None;
    if (btns & Qt::LeftButton)
        result = result | MouseButton::Left;
    if (btns & Qt::RightButton)
        result = result | MouseButton::Right;
    if (btns & Qt::MiddleButton)
        result = result | MouseButton::Middle;
    return result;
}

mulan::engine::KeyModifier DocWidget::translateModifiers(Qt::KeyboardModifiers mods) {
    KeyModifier result = KeyModifier::None;
    if (mods & Qt::ControlModifier)
        result = result | KeyModifier::Ctrl;
    if (mods & Qt::ShiftModifier)
        result = result | KeyModifier::Shift;
    if (mods & Qt::AltModifier)
        result = result | KeyModifier::Alt;
    return result;
}

mulan::engine::Key DocWidget::translateKey(int qtKey) {
    switch (qtKey) {
    case Qt::Key_Escape: return Key::Escape;
    case Qt::Key_Return:
    case Qt::Key_Enter: return Key::Enter;
    case Qt::Key_Space: return Key::Space;
    case Qt::Key_Tab: return Key::Tab;
    case Qt::Key_Backspace: return Key::Backspace;
    case Qt::Key_Delete: return Key::Delete;
    case Qt::Key_Left: return Key::Left;
    case Qt::Key_Right: return Key::Right;
    case Qt::Key_Up: return Key::Up;
    case Qt::Key_Down: return Key::Down;
    case Qt::Key_A: return Key::A;
    case Qt::Key_B: return Key::B;
    case Qt::Key_C: return Key::C;
    case Qt::Key_D: return Key::D;
    case Qt::Key_E: return Key::E;
    case Qt::Key_F: return Key::F;
    case Qt::Key_G: return Key::G;
    case Qt::Key_H: return Key::H;
    case Qt::Key_I: return Key::I;
    case Qt::Key_J: return Key::J;
    case Qt::Key_K: return Key::K;
    case Qt::Key_L: return Key::L;
    case Qt::Key_M: return Key::M;
    case Qt::Key_N: return Key::N;
    case Qt::Key_O: return Key::O;
    case Qt::Key_P: return Key::P;
    case Qt::Key_Q: return Key::Q;
    case Qt::Key_R: return Key::R;
    case Qt::Key_S: return Key::S;
    case Qt::Key_T: return Key::T;
    case Qt::Key_U: return Key::U;
    case Qt::Key_V: return Key::V;
    case Qt::Key_W: return Key::W;
    case Qt::Key_X: return Key::X;
    case Qt::Key_Y: return Key::Y;
    case Qt::Key_Z: return Key::Z;
    case Qt::Key_F1: return Key::F1;
    case Qt::Key_F2: return Key::F2;
    case Qt::Key_F3: return Key::F3;
    case Qt::Key_F4: return Key::F4;
    case Qt::Key_F5: return Key::F5;
    case Qt::Key_F6: return Key::F6;
    case Qt::Key_F7: return Key::F7;
    case Qt::Key_F8: return Key::F8;
    case Qt::Key_F9: return Key::F9;
    case Qt::Key_F10: return Key::F10;
    case Qt::Key_F11: return Key::F11;
    case Qt::Key_F12: return Key::F12;
    default: return Key::Unknown;
    }
}
