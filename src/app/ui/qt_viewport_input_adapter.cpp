/**
 * @file qt_viewport_input_adapter.cpp
 * @brief QtViewportInputAdapter 实现。
 * @author hxxcxx
 * @date 2026-07-14
 */

#include "qt_viewport_input_adapter.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPoint>
#include <QPointF>
#include <QGuiApplication>

namespace mulan::app {

QtViewportInputAdapter::QtViewportInputAdapter(qreal devicePixelRatioF) : dpr_(devicePixelRatioF) {
}

engine::InputEvent QtViewportInputAdapter::mousePress(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    engine::InputEvent ev =
            engine::InputEvent::mousePress(p.x(), p.y(), translateButton(e.button()), translateButtons(e.buttons()),
                                           translateModifiers(e.modifiers()));
    ev.timestampMs = static_cast<uint64_t>(e.timestamp());
    return ev;
}

engine::InputEvent QtViewportInputAdapter::mouseRelease(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    engine::InputEvent ev =
            engine::InputEvent::mouseRelease(p.x(), p.y(), translateButton(e.button()), translateButtons(e.buttons()),
                                             translateModifiers(e.modifiers()));
    ev.timestampMs = static_cast<uint64_t>(e.timestamp());
    return ev;
}

engine::InputEvent QtViewportInputAdapter::mouseMove(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    engine::InputEvent ev = engine::InputEvent::mouseMove(p.x(), p.y(), translateButtons(e.buttons()),
                                                          translateModifiers(e.modifiers()));
    ev.timestampMs = static_cast<uint64_t>(e.timestamp());
    return ev;
}

engine::InputEvent QtViewportInputAdapter::mouseDoubleClick(const QMouseEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    engine::InputEvent ev;
    ev.type = engine::InputEvent::Type::MouseDoubleClick;
    ev.button = translateButton(e.button());
    ev.buttons = translateButtons(e.buttons());
    ev.modifiers = translateModifiers(e.modifiers());
    ev.x = p.x();
    ev.y = p.y();
    ev.timestampMs = static_cast<uint64_t>(e.timestamp());
    return ev;
}

engine::InputEvent QtViewportInputAdapter::wheel(const QWheelEvent& e) const {
    const QPoint p = framebufferEventPosition(e.position());
    const QPoint pixelDelta = e.pixelDelta();
    const float delta = !pixelDelta.isNull() ? static_cast<float>(pixelDelta.y()) / 120.0f
                                             : static_cast<float>(e.angleDelta().y()) / 120.0f;
    engine::InputEvent ev = engine::InputEvent::wheel(p.x(), p.y(), delta, translateModifiers(e.modifiers()));
    ev.timestampMs = static_cast<uint64_t>(e.timestamp());
    return ev;
}

engine::InputEvent QtViewportInputAdapter::keyPress(const QKeyEvent& e) const {
    engine::InputEvent ev = engine::InputEvent::keyPress(translateKey(e.key()), translateModifiers(e.modifiers()));
    ev.autoRepeat = e.isAutoRepeat();
    ev.timestampMs = static_cast<uint64_t>(e.timestamp());
    return ev;
}

engine::InputEvent QtViewportInputAdapter::keyRelease(const QKeyEvent& e) const {
    engine::InputEvent ev = engine::InputEvent::keyRelease(translateKey(e.key()), translateModifiers(e.modifiers()));
    ev.autoRepeat = e.isAutoRepeat();
    ev.timestampMs = static_cast<uint64_t>(e.timestamp());
    return ev;
}

QPointF QtViewportInputAdapter::framebufferPosition(const QPointF& logicalPos) const {
    return QPointF(logicalPos.x() * dpr_, logicalPos.y() * dpr_);
}

QPoint QtViewportInputAdapter::framebufferEventPosition(const QPointF& logicalPos) const {
    // Qt 鼠标事件给的是 widget logical coordinates；RHI swapchain、Camera::screenRay()
    // 和 ViewCubeModel::pickPart() 使用的是 framebuffer coordinates。
    // 高 DPI 屏幕下两者不同，必须在进入 view/engine picking 前乘 devicePixelRatioF()。
    return QPoint(qRound(logicalPos.x() * dpr_), qRound(logicalPos.y() * dpr_));
}

engine::MouseButton QtViewportInputAdapter::translateButton(Qt::MouseButton btn) {
    switch (btn) {
    case Qt::LeftButton: return engine::MouseButton::Left;
    case Qt::RightButton: return engine::MouseButton::Right;
    case Qt::MiddleButton: return engine::MouseButton::Middle;
    default: return engine::MouseButton::None;
    }
}

engine::MouseButton QtViewportInputAdapter::translateButtons(Qt::MouseButtons btns) {
    engine::MouseButton result = engine::MouseButton::None;
    if (btns & Qt::LeftButton)
        result = result | engine::MouseButton::Left;
    if (btns & Qt::RightButton)
        result = result | engine::MouseButton::Right;
    if (btns & Qt::MiddleButton)
        result = result | engine::MouseButton::Middle;
    return result;
}

engine::KeyModifier QtViewportInputAdapter::translateModifiers(Qt::KeyboardModifiers mods) {
    engine::KeyModifier result = engine::KeyModifier::None;
    if (mods & Qt::ControlModifier)
        result = result | engine::KeyModifier::Ctrl;
    if (mods & Qt::ShiftModifier)
        result = result | engine::KeyModifier::Shift;
    if (mods & Qt::AltModifier)
        result = result | engine::KeyModifier::Alt;
    return result;
}

engine::Key QtViewportInputAdapter::translateKey(int qtKey) {
    switch (qtKey) {
    case Qt::Key_Escape: return engine::Key::Escape;
    case Qt::Key_Return:
    case Qt::Key_Enter: return engine::Key::Enter;
    case Qt::Key_Space: return engine::Key::Space;
    case Qt::Key_Tab: return engine::Key::Tab;
    case Qt::Key_Backspace: return engine::Key::Backspace;
    case Qt::Key_Delete: return engine::Key::Delete;
    case Qt::Key_Left: return engine::Key::Left;
    case Qt::Key_Right: return engine::Key::Right;
    case Qt::Key_Up: return engine::Key::Up;
    case Qt::Key_Down: return engine::Key::Down;
    case Qt::Key_A: return engine::Key::A;
    case Qt::Key_B: return engine::Key::B;
    case Qt::Key_C: return engine::Key::C;
    case Qt::Key_D: return engine::Key::D;
    case Qt::Key_E: return engine::Key::E;
    case Qt::Key_F: return engine::Key::F;
    case Qt::Key_G: return engine::Key::G;
    case Qt::Key_H: return engine::Key::H;
    case Qt::Key_I: return engine::Key::I;
    case Qt::Key_J: return engine::Key::J;
    case Qt::Key_K: return engine::Key::K;
    case Qt::Key_L: return engine::Key::L;
    case Qt::Key_M: return engine::Key::M;
    case Qt::Key_N: return engine::Key::N;
    case Qt::Key_O: return engine::Key::O;
    case Qt::Key_P: return engine::Key::P;
    case Qt::Key_Q: return engine::Key::Q;
    case Qt::Key_R: return engine::Key::R;
    case Qt::Key_S: return engine::Key::S;
    case Qt::Key_T: return engine::Key::T;
    case Qt::Key_U: return engine::Key::U;
    case Qt::Key_V: return engine::Key::V;
    case Qt::Key_W: return engine::Key::W;
    case Qt::Key_X: return engine::Key::X;
    case Qt::Key_Y: return engine::Key::Y;
    case Qt::Key_Z: return engine::Key::Z;
    case Qt::Key_F1: return engine::Key::F1;
    case Qt::Key_F2: return engine::Key::F2;
    case Qt::Key_F3: return engine::Key::F3;
    case Qt::Key_F4: return engine::Key::F4;
    case Qt::Key_F5: return engine::Key::F5;
    case Qt::Key_F6: return engine::Key::F6;
    case Qt::Key_F7: return engine::Key::F7;
    case Qt::Key_F8: return engine::Key::F8;
    case Qt::Key_F9: return engine::Key::F9;
    case Qt::Key_F10: return engine::Key::F10;
    case Qt::Key_F11: return engine::Key::F11;
    case Qt::Key_F12: return engine::Key::F12;
    default: return engine::Key::Unknown;
    }
}

}  // namespace mulan::app
