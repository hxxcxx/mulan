/**
 * @file main_window.h
 * @brief Qt 主窗口 — SARibbon + 多文档
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "../services/document_open_service.h"
#include "../services/recent_thumbnail_service.h"

#include <mulan/editor/command/command_manager.h>

#include <SARibbon.h>
#include <mulan/view/core/view_state.h>

#include <string>
#include <string_view>
#include <vector>

class DocumentWorkspace;
class DocumentViewport;
class EngineSettingsDialog;
class ProfilerWindow;
class QCloseEvent;

class MainWindow : public SARibbonMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onNewDocument();
    void onOpenFile();
    void onCurrentDocumentChanged(const QString& name);
    void onEngineSettings();
    void onProfiler();

private:
    // --- UI 构建 ---
    void buildRibbon();
    void buildRibbonHomeCategory();
    void buildRibbonViewCategory();
    void buildQuickAccessBar();
    void buildRightButtonBar();
    void setCurrentRenderMode(mulan::view::RenderMode mode);
    void updateDisplayActions();
    void updateCommandActions();
    QAction* createCommandAction(const QString& iconPath, std::string_view commandId);
    void bindCommandAction(QAction* action, std::string_view commandId);
    mulan::editor::CommandHost currentCommandHost() const;
    void executeCommand(std::string_view id);
    bool openFilePath(const QString& filePath, bool recordRecent = true);
    void completeFileOpen(DocumentOpenService::RequestId requestId, QString filePath, bool recordRecent,
                          mulan::Result<mulan::io::OpenDocumentResult> opened);

    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

    // --- 核心组件 ---
    DocumentWorkspace* document_workspace_ = nullptr;

    // --- 文档管理 ---
    mulan::editor::CommandManager command_manager_;
    RecentThumbnailService recent_thumbnail_service_;
    DocumentOpenService document_open_service_;

    struct CommandActionBinding {
        std::string commandId;
        QAction* action = nullptr;
    };
    std::vector<CommandActionBinding> command_actions_;

    // --- Actions ---
    QAction* action_new_ = nullptr;
    QAction* action_open_ = nullptr;
    QAction* action_exit_ = nullptr;
    QAction* action_undo_ = nullptr;
    QAction* action_redo_ = nullptr;
    QAction* action_fit_all_ = nullptr;
    QAction* action_perspective_projection_ = nullptr;
    QAction* action_draw_line_ = nullptr;
    QAction* action_draw_polyline_ = nullptr;
    QAction* action_draw_circle_ = nullptr;
    QAction* action_draw_bezier_ = nullptr;
    QAction* action_draw_bspline_ = nullptr;
    QAction* action_draw_nurbs_ = nullptr;
    QAction* action_draw_face_ = nullptr;
    QAction* action_model_extrude_ = nullptr;
    QAction* action_edit_move_ = nullptr;
    QAction* action_edit_copy_ = nullptr;
    QAction* action_edit_delete_ = nullptr;
    QAction* action_engine_settings_ = nullptr;
    QAction* action_display_edges_ = nullptr;
    QAction* action_display_wireframe_ = nullptr;
    QAction* action_display_shaded_ = nullptr;
    QAction* action_show_cube_ = nullptr;
    QAction* action_profiler_ = nullptr;
    ProfilerWindow* profiler_window_ = nullptr;

    // --- Ribbon 结构：Home ---
    SARibbonCategory* category_home_ = nullptr;
    SARibbonPanel* panel_file_ = nullptr;
    SARibbonPanel* panel_draw_ = nullptr;
    SARibbonPanel* panel_edit_ = nullptr;
    SARibbonPanel* panel_view_ = nullptr;
    SARibbonPanel* panel_setting_ = nullptr;

    // --- Ribbon 结构：View ---
    SARibbonCategory* category_view_ = nullptr;
    SARibbonPanel* panel_display_ = nullptr;
    SARibbonPanel* panel_profiler_ = nullptr;
};
