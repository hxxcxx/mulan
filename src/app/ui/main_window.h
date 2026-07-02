/**
 * @file main_window.h
 * @brief Qt 主窗口 — SARibbon + 多文档
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include <SARibbon.h>
#include <mulan/io/file_manager.h>

class DocumentArea;
class DocumentSession;
class EngineSettingsDialog;

class MainWindow : public SARibbonMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onOpenFile();
    void onCurrentDocumentChanged(const QString& name);
    void onEngineSettings();

private:
    // --- UI 构建 ---
    void buildRibbon();
    void buildRibbonHomeCategory();
    void buildRibbonViewCategory();
    void buildQuickAccessBar();
    void buildRightButtonBar();

    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

    // --- 核心组件 ---
    DocumentArea* doc_area_ = nullptr;

    // --- 文档管理 ---
    mulan::io::FileManager doc_manager_;

    // --- Actions ---
    QAction* action_open_   = nullptr;
    QAction* action_exit_   = nullptr;
    QAction* action_fit_all_ = nullptr;
    QAction* action_engine_settings_ = nullptr;

    // --- Ribbon 结构：Home ---
    SARibbonCategory* category_home_     = nullptr;
    SARibbonPanel*    panel_file_        = nullptr;
    SARibbonPanel*    panel_view_        = nullptr;
    SARibbonPanel*    panel_setting_     = nullptr;

    // --- Ribbon 结构：View ---
    SARibbonCategory* category_view_     = nullptr;
    SARibbonPanel*    panel_display_     = nullptr;
};
