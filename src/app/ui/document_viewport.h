/**
 * @file document_viewport.h
 * @brief 承载文档视图运行时的 Qt 渲染控件。
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-01 (重构) / 2026-07-14 (输入 adapter 抽离 + 状态下移)
 *       / 2026-07-15 (帧失效合并与渲染线程健康状态)
 *
 * 2026-07-14：DocumentViewport 不再持有任何编辑语义状态（click/drag/consumed）。
 * 这些职责已下移到 DocumentView。DocumentViewport 只做 Qt 转换、取消路由和结果应用。
 */
#pragma once

#include "qt_viewport_input_adapter.h"

#include <mulan/editor/document/document_view.h>
#include <mulan/interaction/input_event.h>
#include <mulan/view/core/view_config.h>

#include <QObject>
#include <QString>
#include <QWidget>

#include <atomic>
#include <cstdint>

class DocumentViewport : public QWidget {
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

    explicit DocumentViewport(const mulan::view::ViewConfig& viewConfig, QWidget* parent = nullptr);
    ~DocumentViewport();

    void setDocumentSession(mulan::editor::DocumentSession* session);

    bool init();
    void shutdown();

    bool isReady() const;
    mulan::editor::CommandHost commandHost();
    mulan::view::RenderMode renderMode() const;
    void setRenderMode(mulan::view::RenderMode mode);
    mulan::view::SurfaceShading surfaceShading() const;
    void setSurfaceShading(mulan::view::SurfaceShading shading);
    bool viewCubeVisible() const;
    void setViewCubeVisible(bool visible);
    mulan::Result<mulan::view::CaptureImage> capture(mulan::view::CaptureRequest request);

signals:
    void commandStateInvalidated();
    void runtimeFailed(const QString& message);

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
    /// 只登记帧失效；同一轮 Qt 事件循环内的多次请求合并为一次提交。
    void requestFrame();
    /// 严格执行 DocumentView 返回的结果，不再根据 consumed/activeTool 猜测状态。
    void applyResult(const mulan::editor::DocumentInputOutcome& result);
    void queueRuntimeEvent();
    void submitPendingFrame();
    void consumeRuntimeEvent();
    void transitionToRuntimeFailure(QString message);

    // RenderThread 只向该 QObject 投递 queued invocation，不直接接触 DocumentViewport。
    // 它先于 DocumentView 构造，因此析构顺序保证通道完全停止后才销毁代理。
    QObject runtime_event_proxy_;
    mulan::editor::DocumentView document_view_;
    mulan::view::ViewConfig view_config_;
    mulan::app::QtViewportInputAdapter input_adapter_;
    RuntimeState runtime_state_ = RuntimeState::Stopped;
    std::atomic_bool runtime_event_pending_ = false;
    bool frame_invalidated_ = false;
};
