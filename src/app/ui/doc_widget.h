/**
 * @file doc_widget.h
 * @brief 承载文档视图运行时的 Qt 渲染控件。
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-01 (重构) / 2026-07-14 (输入 adapter 抽离 + 状态下移)
 *
 * 2026-07-14：DocWidget 不再持有任何编辑语义状态（click/drag/consumed）。
 * 这些职责已下移到 DocumentView。DocWidget 只做 Qt 转换 + cancel 路由 + 结果应用。
 */
#pragma once

#include <QWidget>

#include <mulan/interaction/input_event.h>
#include <mulan/view/core/view_config.h>
#include <mulan/editor/document/document_view.h>

#include "qt_viewport_input_adapter.h"

class DocumentSession;

class DocWidget : public QWidget {
    Q_OBJECT

public:
    explicit DocWidget(QWidget* parent = nullptr);
    ~DocWidget();

    void setDocumentSession(DocumentSession* session);

    void setViewConfig(const mulan::view::ViewConfig& cfg) { view_config_ = cfg; }
    mulan::view::ViewConfig& viewConfig() { return view_config_; }

    bool init();
    void shutdown();
    void requestFrame();

    DocumentView& documentView() { return document_view_; }
    const DocumentView& documentView() const { return document_view_; }
    mulan::view::ViewContext& viewContext() { return document_view_.viewContext(); }

signals:
    void commandStateInvalidated();

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
    void focusOutEvent(QFocusEvent* e) override;
    bool event(QEvent* e) override;

private:
    /// 严格执行 DocumentView 返回的结果，不再根据 consumed/activeTool 猜测状态。
    void applyResult(const DocumentInputOutcome& result);
    void clearPreview(bool refresh = true);

    DocumentView document_view_;
    mulan::view::ViewConfig view_config_;
    mulan::app::QtViewportInputAdapter input_adapter_;
};
