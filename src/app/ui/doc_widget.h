/**
 * @file doc_widget.h
 * @brief 承载文档视图运行时的 Qt 渲染控件。
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-01 (重构)
 */
#pragma once

#include "document_view_binding.h"

#include <QWidget>
#include <QPoint>

#include <mulan/engine/interaction/input_event.h>
#include <mulan/view/view_context.h>

class DocumentSession;

class DocWidget : public QWidget {
    Q_OBJECT

public:
    explicit DocWidget(QWidget* parent = nullptr);
    ~DocWidget();

    void setDocumentSession(DocumentSession* session);

    void setViewConfig(const mulan::view::ViewConfig& cfg) { view_config_ = cfg; }
    mulan::view::ViewConfig& viewConfig() { return view_config_; }

    void init();
    void requestFrame();
    void fitAll();

    mulan::view::ViewContext& viewContext() { return view_context_; }

protected:
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    QPaintEngine* paintEngine() const override { return nullptr; }

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    static mulan::engine::MouseButton translateButton(Qt::MouseButton btn);
    static mulan::engine::MouseButton translateButtons(Qt::MouseButtons btns);
    static mulan::engine::KeyModifier translateModifiers(Qt::KeyboardModifiers mods);
    static mulan::engine::Key translateKey(int qtKey);

    QPoint framebufferPosition(const QPointF& pos) const;
    mulan::engine::InputEvent makeMousePressEvent(const QMouseEvent& e) const;
    mulan::engine::InputEvent makeMouseReleaseEvent(const QMouseEvent& e) const;
    mulan::engine::InputEvent makeMouseMoveEvent(const QMouseEvent& e) const;
    mulan::engine::InputEvent makeMouseDoubleClickEvent(const QMouseEvent& e) const;
    mulan::engine::InputEvent makeWheelEvent(const QWheelEvent& e) const;
    void updateHoverAtFramebuffer(const QPoint& framebufferPos);
    void selectAtFramebuffer(const QPoint& framebufferPos);

    mulan::view::ViewContext view_context_;
    mulan::view::ViewConfig view_config_;
    DocumentSession* session_ = nullptr;
    DocumentViewBinding binding_;

    QPoint press_pos_;
    bool left_press_pending_ = false;
    bool left_press_dragged_ = false;
};
