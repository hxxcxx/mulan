/**
 * @file doc_widget.h
 * @brief Qt 渲染控件 — Viewport 的薄壳封装
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-01 (重构)
 */
#pragma once

#include <QWidget>

#include <mulan/world/viewport.h>
#include <mulan/engine/interaction/input_event.h>

#include <memory>

class UIDocument;

class DocWidget : public QWidget {
    Q_OBJECT

public:
    explicit DocWidget(QWidget* parent = nullptr);
    ~DocWidget();

    /// 设置当前 UI 文档（绑定场景到视图）
    void setUIDocument(UIDocument* doc);

    /// 设置引擎初始化配置（需在 init() 之前调用）
    void setViewConfig(const mulan::world::ViewConfig& cfg) { view_config_ = cfg; }

    /// 获取可修改的引擎配置引用（需在 init() 之前调用）
    mulan::world::ViewConfig& viewConfig() { return view_config_; }

    /// 初始化 Vulkan 设备与 SwapChain（需在 widget 显示后、渲染前调用）
    void init();

    /// 请求渲染下一帧
    void requestFrame();

    /// 适配相机到整个场景包围盒（Fit All）
    void fitAll();

    /// 访问底层 Viewport
    mulan::world::Viewport& viewport() { return viewport_; }

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

private:
    static mulan::engine::MouseButton translateButton(Qt::MouseButton btn);
    static mulan::engine::MouseButton translateButtons(Qt::MouseButtons btns);
    static mulan::engine::KeyModifier translateModifiers(Qt::KeyboardModifiers mods);
    static mulan::engine::Key translateKey(int qtKey);

    mulan::world::Viewport  viewport_;
    mulan::world::ViewConfig view_config_;
    UIDocument*                   ui_doc_ = nullptr;
};
