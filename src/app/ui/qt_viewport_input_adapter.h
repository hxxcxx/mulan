/**
 * @file qt_viewport_input_adapter.h
 * @brief 把 Qt 鼠标/键盘/滚轮事件转换为平台无关 InputEvent。
 *
 * 职责（且仅限于此）：
 *  - Qt button / key / modifiers 枚举转换；
 *  - logical → framebuffer 坐标转换（乘 devicePixelRatioF）；
 *  - wheel pixel/angle delta 保真；
 *  - 键盘 auto-repeat 标记；
 *  - timestamp 填充。
 *
 * 不负责：click/drag 判定、hover/selection、capture owner、frame 请求。
 * 这些是 ViewportController / DocumentViewport 交互层的职责。
 *
 * @author hxxcxx
 * @date 2026-07-14
 */
#pragma once

#include <mulan/interaction/input_event.h>

#include <QtGlobal>  // qreal
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPoint>
#include <QPointF>

namespace mulan::app {

class QtViewportInputAdapter {
public:
    /// devicePixelRatioF 由 DocumentViewport 在调用时传入（adapter 不持有 QWidget）。
    explicit QtViewportInputAdapter(qreal devicePixelRatioF = 1.0);

    void setDevicePixelRatioF(qreal dpr) { dpr_ = dpr; }
    qreal devicePixelRatioF() const { return dpr_; }

    // ── 事件转换 ──

    engine::InputEvent mousePress(const QMouseEvent& e) const;
    engine::InputEvent mouseRelease(const QMouseEvent& e) const;
    engine::InputEvent mouseMove(const QMouseEvent& e) const;
    engine::InputEvent mouseDoubleClick(const QMouseEvent& e) const;
    engine::InputEvent wheel(const QWheelEvent& e) const;
    engine::InputEvent keyPress(const QKeyEvent& e) const;
    engine::InputEvent keyRelease(const QKeyEvent& e) const;

    /// Qt 生命周期事件 → 取消事件。
    engine::InputEvent pointerCancel() const { return engine::InputEvent::pointerCancel(); }
    engine::InputEvent focusLost() const { return engine::InputEvent::focusLost(); }

    // ── 坐标转换（供 DocumentViewport 做 picking/select 时复用）──

    /// 浮点 framebuffer 坐标，用于 ray picking（避免 DPR 截断误差）。
    QPointF framebufferPosition(const QPointF& logicalPos) const;

    // ── 静态枚举转换 ──

    static engine::MouseButton translateButton(Qt::MouseButton btn);
    static engine::MouseButton translateButtons(Qt::MouseButtons btns);
    static engine::KeyModifier translateModifiers(Qt::KeyboardModifiers mods);
    static engine::Key translateKey(int qtKey);

private:
    /// 四舍五入的 framebuffer 坐标，用于事件分发路径。
    QPoint framebufferEventPosition(const QPointF& logicalPos) const;

    qreal dpr_ = 1.0;
};

}  // namespace mulan::app
