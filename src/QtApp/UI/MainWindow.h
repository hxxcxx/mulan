/**
 * @file MainWindow.h
 * @brief Qt 主窗口 — SARibbon + 多文档
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include <SARibbon.h>
#include <mulan/Document/DocumentManager.h>

class DocumentArea;
class UIDocument;
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
    DocumentArea* m_docArea = nullptr;

    // --- 文档管理 ---
    mulan::document::DocumentManager m_docManager;

    // --- Actions ---
    QAction* m_actionOpen   = nullptr;
    QAction* m_actionExit   = nullptr;
    QAction* m_actionFitAll = nullptr;
    QAction* m_actionEngineSettings = nullptr;

    // --- Ribbon 结构：Home ---
    SARibbonCategory* m_categoryHome     = nullptr;
    SARibbonPanel*    m_panelFile        = nullptr;
    SARibbonPanel*    m_panelView        = nullptr;
    SARibbonPanel*    m_panelSetting     = nullptr;

    // --- Ribbon 结构：View ---
    SARibbonCategory* m_categoryView     = nullptr;
    SARibbonPanel*    m_panelDisplay     = nullptr;
};
