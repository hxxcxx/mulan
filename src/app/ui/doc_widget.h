/**
 * @file doc_widget.h
 * @brief 承载文档视图运行时的 Qt 渲染控件。
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-01 (重构) / 2026-07-14 (输入 adapter 抽离 + 状态下移)
 *       / 2026-07-15 (帧失效合并与渲染线程健康状态)
 *
 * 2026-07-14：DocWidget 不再持有任何编辑语义状态（click/drag/consumed）。
 * 这些职责已下移到 DocumentView。DocWidget 只做 Qt 转换 + cancel 路由 + 结果应用。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QWidget>

#include <mulan/interaction/input_event.h>
#include <mulan/view/core/view_config.h>
#include <mulan/editor/document/document_view.h>

#include "qt_viewport_input_adapter.h"

#include <atomic>
#include <cstdint>

class DocumentSession;

class DocWidget : public QWidget {
    Q_OBJECT

public:
    enum class RuntimeState : uint8_t {
        Stopped,
        Starting,
        Ready,
        Failed,
        Stopping,
    };
    Q_ENUM(RuntimeState)

    explicit DocWidget(QWidget* parent = nullptr);
    ~DocWidget();

    void setDocumentSession(DocumentSession* session);

    void setViewConfig(const mulan::view::ViewConfig& cfg) { view_config_ = cfg; }
    mulan::view::ViewConfig& viewConfig() { return view_config_; }

    bool init();
    void shutdown();
    /// 只登记帧失效；同一轮 Qt 事件循环内的多次请求合并为一次提交。
    void requestFrame();

    RuntimeState runtimeState() const { return runtime_state_; }

    DocumentView& documentView() { return document_view_; }
    const DocumentView& documentView() const { return document_view_; }
    mulan::view::ViewContext& viewContext() { return document_view_.viewContext(); }

signals:
    void commandStateInvalidated();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* e) override;
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
    void queueRuntimeEvent();
    void submitPendingFrame();
    void consumeRuntimeEvent();
    void transitionToRuntimeFailure(QString message);

    // RenderThread 只向该 QObject 投递 queued invocation，不直接接触 DocWidget。
    // 它先于 DocumentView 构造，因此析构顺序保证通道完全停止后才销毁代理。
    QObject runtime_event_proxy_;
    DocumentView document_view_;
    mulan::view::ViewConfig view_config_;
    mulan::app::QtViewportInputAdapter input_adapter_;
    RuntimeState runtime_state_ = RuntimeState::Stopped;
    std::atomic_bool runtime_event_pending_ = false;
    bool frame_invalidated_ = false;
};
