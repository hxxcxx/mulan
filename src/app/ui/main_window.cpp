#include "main_window.h"
#include "document_workspace.h"
#include "document_viewport.h"
#include "engine_settings.h"
#include "engine_settings_dialog.h"
#include "profiler_window.h"
#include <mulan/editor/document/document_session.h>
#include <mulan/editor/command/builtin_commands.h>

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/io/import_result.h>

#include <QFileDialog>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QFileInfo>
#include <QMenu>
#include <QActionGroup>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <QToolButton>

#include <algorithm>
#include <memory>

namespace {

void logImportReport(const mulan::io::ImportReport& report) {
    LOG_INFO(
            "[App] Document import report: entities={}, meshes={}, breps={}, primitives={}, materials={}, textures={}, "
            "lights={}, warnings={}",
            report.entityCount, report.meshAssetCount, report.brepAssetCount, report.primitiveCount,
            report.materialCount, report.textureCount, report.lightCount, report.warnings.size());
    for (const auto& warning : report.warnings) {
        LOG_WARN("[App] Import warning: {}", warning);
    }
}

mulan::editor::DocumentSessionOptions importedSessionOptions(const mulan::io::ImportReport& report) {
    const bool isCad =
            report.brepAssetCount > 0 && (report.meshAssetCount == 0 || report.brepAssetCount >= report.meshAssetCount);
    return mulan::editor::DocumentSessionOptions{
        .kind = mulan::editor::DocumentSessionKind::Imported,
        .renderPreferences =
                {
                        .preferOrthographic = isCad,
                        .preferIBL = false,
                },
    };
}

}  // namespace

//===================================================
// MainWindow
//===================================================

MainWindow::MainWindow(QWidget* parent) : SARibbonMainWindow(parent) {
    setWindowTitle({});
    resize(1280, 720);
    setAcceptDrops(true);

    // 中央多文档区
    document_workspace_ = new DocumentWorkspace(this);
    setCentralWidget(document_workspace_);
    mulan::editor::registerBuiltinCommands(command_manager_);

    connect(document_workspace_, &DocumentWorkspace::currentDocumentChanged, this,
            &MainWindow::onCurrentDocumentChanged);
    connect(document_workspace_, &DocumentWorkspace::currentDocumentCommandStateInvalidated, this,
            &MainWindow::updateDisplayActions);
    connect(document_workspace_, &DocumentWorkspace::currentDocumentRuntimeFailed, this,
            [this](const QString& message) {
                statusBar()->showMessage(tr("Rendering stopped: %1").arg(message));
                updateDisplayActions();
            });
    connect(document_workspace_, &DocumentWorkspace::documentClosing, this,
            [this](DocumentViewport* viewport, const QString& filePath) {
                if (!viewport) {
                    return;
                }
                const QString thumbnailPath = recent_thumbnail_service_.capture(*viewport, filePath);
                if (!thumbnailPath.isEmpty()) {
                    document_workspace_->setRecentThumbnail(filePath, thumbnailPath);
                }
            });
    connect(document_workspace_, &DocumentWorkspace::startupNewRequested, this, &MainWindow::onNewDocument);
    connect(document_workspace_, &DocumentWorkspace::startupOpenRequested, this, &MainWindow::onOpenFile);
    connect(document_workspace_, &DocumentWorkspace::startupRecentFileRequested, this,
            [this](const QString& path) { openFilePath(path); });

    // 构建 Ribbon
    buildRibbon();

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!document_workspace_ || document_workspace_->closeAllDocuments()) {
        e->accept();
        return;
    }
    e->ignore();
}

//===================================================
// Ribbon 构建
//===================================================

void MainWindow::buildRibbon() {
    auto* ribbon = ribbonBar();
    // 使用默认的 LooseThreeRow 风格，大按钮有足够高度显示 32x32 图标
    ribbon->showMinimumModeButton();
    if (auto* rightControls = ribbon->rightButtonGroup()) {
        rightControls->setObjectName("ribbonRightControls");
        rightControls->setIconSize(QSize(18, 18));
        for (QToolButton* button : rightControls->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly)) {
            button->setProperty("uiRole", "ribbonCollapse");
        }
    }

    buildRibbonHomeCategory();
    buildRibbonViewCategory();
    buildQuickAccessBar();
    buildRightButtonBar();
}

QAction* MainWindow::createCommandAction(const QString& iconPath, std::string_view commandId) {
    const mulan::editor::CommandState state = command_manager_.state(commandId, currentCommandHost());
    auto* action = new QAction(QIcon(iconPath), QString::fromStdString(state.title), this);
    action->setEnabled(state.enabled);
    action->setVisible(state.visible);
    action->setCheckable(state.checkable);
    action->setChecked(state.checkable && state.checked);
    action->setShortcut(QKeySequence(QString::fromStdString(state.shortcut)));
    action->setStatusTip(QString::fromStdString(state.statusText));
    action->setToolTip(QString::fromStdString(state.statusText.empty() ? state.title : state.statusText));
    bindCommandAction(action, commandId);
    connect(action, &QAction::triggered, this, [this, command = std::string(commandId)]() { executeCommand(command); });
    return action;
}

void MainWindow::bindCommandAction(QAction* action, std::string_view commandId) {
    if (!action || commandId.empty()) {
        return;
    }
    command_actions_.push_back(CommandActionBinding{ std::string(commandId), action });
}

void MainWindow::updateCommandActions() {
    const mulan::editor::CommandHost host = currentCommandHost();
    for (const CommandActionBinding& binding : command_actions_) {
        if (!binding.action) {
            continue;
        }

        const mulan::editor::CommandState state = command_manager_.state(binding.commandId, host);
        const QSignalBlocker blocker(binding.action);
        binding.action->setText(QString::fromStdString(state.title));
        binding.action->setEnabled(state.enabled);
        binding.action->setVisible(state.visible);
        binding.action->setCheckable(state.checkable);
        binding.action->setChecked(state.checkable && state.checked);
        binding.action->setShortcut(QKeySequence(QString::fromStdString(state.shortcut)));
        binding.action->setStatusTip(QString::fromStdString(state.statusText));
        binding.action->setToolTip(QString::fromStdString(state.statusText.empty() ? state.title : state.statusText));
    }
}

void MainWindow::buildRibbonHomeCategory() {
    category_home_ = new SARibbonCategory(this);
    category_home_->setCategoryName(tr("Home"));

    // ── File 面板 ──
    panel_file_ = new SARibbonPanel(tr("File"), category_home_);
    action_new_ = new QAction(QIcon(":/app/icons/icon/file-new.svg"), tr("New"), this);
    action_new_->setShortcut(QKeySequence::New);
    connect(action_new_, &QAction::triggered, this, &MainWindow::onNewDocument);
    panel_file_->addLargeAction(action_new_);

    action_open_ = new QAction(QIcon(":/app/icons/icon/file-open.svg"), tr("Open"), this);
    connect(action_open_, &QAction::triggered, this, &MainWindow::onOpenFile);
    panel_file_->addLargeAction(action_open_);

    category_home_->addPanel(panel_file_);

    panel_draw_ = new SARibbonPanel(tr("Draw"), category_home_);
    action_draw_line_ = createCommandAction(":/app/icons/icon/draw-line.svg", "draw.line");
    panel_draw_->addLargeAction(action_draw_line_);

    action_draw_polyline_ = createCommandAction(":/app/icons/icon/draw-polyline.svg", "draw.polyline");
    panel_draw_->addLargeAction(action_draw_polyline_);

    action_draw_circle_ = createCommandAction(":/app/icons/icon/draw-circle.svg", "draw.circle");
    panel_draw_->addLargeAction(action_draw_circle_);

    action_draw_bezier_ = createCommandAction(":/app/icons/icon/draw-bezier.svg", "draw.bezier");
    panel_draw_->addLargeAction(action_draw_bezier_);

    action_draw_bspline_ = createCommandAction(":/app/icons/icon/draw-bspline.svg", "draw.bspline");
    panel_draw_->addLargeAction(action_draw_bspline_);

    action_draw_nurbs_ = createCommandAction(":/app/icons/icon/draw-nurbs.svg", "draw.nurbs");
    panel_draw_->addLargeAction(action_draw_nurbs_);

    action_draw_face_ = createCommandAction(":/app/icons/icon/draw-face.svg", "draw.face");
    panel_draw_->addLargeAction(action_draw_face_);

    category_home_->addPanel(panel_draw_);

    panel_edit_ = new SARibbonPanel(tr("Edit"), category_home_);
    action_edit_move_ = createCommandAction(":/app/icons/icon/edit-move.svg", "edit.move");
    panel_edit_->addLargeAction(action_edit_move_);

    action_edit_copy_ = createCommandAction(":/app/icons/icon/edit-copy.svg", "edit.copy");
    panel_edit_->addLargeAction(action_edit_copy_);

    action_edit_delete_ = createCommandAction(":/app/icons/icon/edit-delete.svg", "edit.delete");
    panel_edit_->addLargeAction(action_edit_delete_);

    action_model_extrude_ = createCommandAction(":/app/icons/icon/model-extrude.svg", "model.extrude");
    panel_edit_->addLargeAction(action_model_extrude_);
    category_home_->addPanel(panel_edit_);

    // ── Navigation 面板 ──
    panel_view_ = new SARibbonPanel(tr("Navigation"), category_home_);
    action_fit_all_ = createCommandAction(":/app/icons/icon/view-fit-all.svg", "view.fitAll");
    panel_view_->addLargeAction(action_fit_all_);
    action_perspective_projection_ =
            createCommandAction(":/app/icons/icon/view-projection.svg", "view.projection.perspective");
    panel_view_->addLargeAction(action_perspective_projection_);
    category_home_->addPanel(panel_view_);

#if defined(MULAN_PROFILER_BACKEND_BUILTIN) && defined(MULAN_ENABLE_PROFILING) && MULAN_ENABLE_PROFILING
    panel_profiler_ = new SARibbonPanel(tr("Diagnostics"), category_home_);
    action_profiler_ = new QAction(QIcon(":/app/icons/icon/profiler.svg"), tr("Profiler"), this);
    action_profiler_->setShortcut(QKeySequence("Ctrl+Shift+P"));
    action_profiler_->setToolTip(tr("Open Mulan Profiler controls"));
    connect(action_profiler_, &QAction::triggered, this, &MainWindow::onProfiler);
    panel_profiler_->addLargeAction(action_profiler_);
    category_home_->addPanel(panel_profiler_);
#endif

    // ── Setting 面板 ──
    panel_setting_ = new SARibbonPanel(tr("Setting"), category_home_);
    auto* actionAbout = new QAction(QIcon(":/app/icons/icon/app-about.svg"), tr("About"), this);
    connect(actionAbout, &QAction::triggered, this,
            [this]() { QMessageBox::about(this, tr("About mulan"), tr("mulan v1.0\nA model geometry viewer.")); });
    panel_setting_->addLargeAction(actionAbout);

    action_engine_settings_ = new QAction(QIcon(":/app/icons/icon/app-settings.svg"), tr("Engine"), this);
    connect(action_engine_settings_, &QAction::triggered, this, &MainWindow::onEngineSettings);
    panel_setting_->addLargeAction(action_engine_settings_);

    category_home_->addPanel(panel_setting_);

    ribbonBar()->addCategoryPage(category_home_);
}

void MainWindow::buildRibbonViewCategory() {
    category_view_ = new SARibbonCategory(this);
    category_view_->setCategoryName(tr("View"));

    panel_display_ = new SARibbonPanel(tr("Display"), category_view_);

    // 线框模式
    action_display_wireframe_ = new QAction(QIcon(":/app/icons/icon/view-wireframe.svg"), tr("Wireframe"), this);
    action_display_wireframe_->setCheckable(true);
    connect(action_display_wireframe_, &QAction::triggered, this,
            [this]() { setCurrentRenderMode(mulan::view::RenderMode::Wireframe); });
    panel_display_->addLargeAction(action_display_wireframe_);

    // 实体模式
    action_display_shaded_ = new QAction(QIcon(":/app/icons/icon/view-shaded.svg"), tr("Shaded"), this);
    action_display_shaded_->setCheckable(true);
    action_display_shaded_->setChecked(true);
    connect(action_display_shaded_, &QAction::triggered, this,
            [this]() { setCurrentRenderMode(mulan::view::RenderMode::Shaded); });
    panel_display_->addLargeAction(action_display_shaded_);

    action_display_edges_ = new QAction(QIcon(":/app/icons/icon/view-edges.svg"), tr("Edges"), this);
    action_display_edges_->setCheckable(true);
    connect(action_display_edges_, &QAction::triggered, this,
            [this]() { setCurrentRenderMode(mulan::view::RenderMode::ShadedWithEdges); });
    panel_display_->addLargeAction(action_display_edges_);

    // 互斥
    auto* displayGroup = new QActionGroup(this);
    displayGroup->addAction(action_display_wireframe_);
    displayGroup->addAction(action_display_shaded_);
    displayGroup->addAction(action_display_edges_);

    panel_display_->addSeparator();

    action_show_cube_ = new QAction(QIcon(":/app/icons/icon/view-cube.svg"), tr("Show Cube"), this);
    action_show_cube_->setCheckable(true);
    action_show_cube_->setChecked(true);
    connect(action_show_cube_, &QAction::toggled, this, [this](bool checked) {
        auto* doc = document_workspace_ ? document_workspace_->currentViewport() : nullptr;
        if (!doc) {
            updateDisplayActions();
            return;
        }

        doc->setViewCubeVisible(checked);
        updateDisplayActions();
    });
    panel_display_->addSmallAction(action_show_cube_);

    category_view_->addPanel(panel_display_);

    ribbonBar()->addCategoryPage(category_view_);

    updateDisplayActions();
}

void MainWindow::onProfiler() {
    if (!profiler_window_)
        profiler_window_ = new ProfilerWindow(this);
    profiler_window_->show();
    profiler_window_->raise();
    profiler_window_->activateWindow();
}

void MainWindow::buildQuickAccessBar() {
    auto* bar = ribbonBar()->quickAccessBar();
    if (!bar)
        return;

    bar->setObjectName("quickAccessBar");
    bar->setIconSize(QSize(18, 18));

    action_undo_ = createCommandAction(":/app/icons/icon/history-undo.svg", "edit.undo");
    bar->addAction(action_undo_);

    action_redo_ = createCommandAction(":/app/icons/icon/history-redo.svg", "edit.redo");
    bar->addAction(action_redo_);

    for (QAction* action : { action_undo_, action_redo_ }) {
        if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(action))) {
            button->setProperty("uiRole", "quickAccess");
        }
    }
}

void MainWindow::buildRightButtonBar() {
    // 暂无右侧按钮栏
}

//===================================================
// 文档操作
//===================================================

mulan::editor::CommandHost MainWindow::currentCommandHost() const {
    auto* doc = document_workspace_ ? document_workspace_->currentViewport() : nullptr;
    return doc ? doc->commandHost() : mulan::editor::CommandHost{};
}

void MainWindow::executeCommand(std::string_view id) {
    mulan::editor::CommandHost host = currentCommandHost();
    auto result = command_manager_.execute(id, host);
    if (!result) {
        statusBar()->showMessage(QString::fromStdString(result.error().message));
    }
    updateDisplayActions();
}

void MainWindow::onCurrentDocumentChanged(const QString& name) {
    if (name.isEmpty()) {
        statusBar()->showMessage("Ready");
    } else {
        statusBar()->showMessage("Active: " + name);
    }
    updateDisplayActions();
}

void MainWindow::setCurrentRenderMode(mulan::view::RenderMode mode) {
    auto* doc = document_workspace_ ? document_workspace_->currentViewport() : nullptr;
    if (!doc || !doc->hasDocumentSession()) {
        updateDisplayActions();
        return;
    }

    doc->setRenderMode(mode);
    updateDisplayActions();
}

void MainWindow::updateDisplayActions() {
    auto* doc = document_workspace_ ? document_workspace_->currentViewport() : nullptr;
    const bool hasDocument = doc && doc->isReady() && doc->hasDocumentSession();
    updateCommandActions();

    const bool hasVisibleDrawAction = (action_draw_line_ && action_draw_line_->isVisible()) ||
                                      (action_draw_polyline_ && action_draw_polyline_->isVisible()) ||
                                      (action_draw_circle_ && action_draw_circle_->isVisible()) ||
                                      (action_draw_bezier_ && action_draw_bezier_->isVisible()) ||
                                      (action_draw_bspline_ && action_draw_bspline_->isVisible()) ||
                                      (action_draw_nurbs_ && action_draw_nurbs_->isVisible()) ||
                                      (action_draw_face_ && action_draw_face_->isVisible());
    if (panel_draw_) {
        panel_draw_->setVisible(hasVisibleDrawAction);
    }

    if (action_display_wireframe_) {
        action_display_wireframe_->setEnabled(hasDocument);
    }
    if (action_display_shaded_) {
        action_display_shaded_->setEnabled(hasDocument);
    }
    if (action_display_edges_) {
        action_display_edges_->setEnabled(hasDocument);
    }
    if (action_show_cube_) {
        action_show_cube_->setEnabled(hasDocument);
    }
    if (!hasDocument)
        return;

    const auto mode = doc->renderMode();
    if (action_display_wireframe_) {
        action_display_wireframe_->setChecked(mode == mulan::view::RenderMode::Wireframe);
    }
    if (action_display_shaded_) {
        action_display_shaded_->setChecked(mode == mulan::view::RenderMode::Shaded);
    }
    if (action_display_edges_) {
        action_display_edges_->setChecked(mode == mulan::view::RenderMode::ShadedWithEdges);
    }

    if (action_show_cube_) {
        const QSignalBlocker blocker(action_show_cube_);
        action_show_cube_->setChecked(doc->viewCubeVisible());
    }
}

void MainWindow::onEngineSettings() {
    EngineSettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::onNewDocument() {
    static int untitledIndex = 1;

    const QString title = tr("Untitled %1").arg(untitledIndex++);
    auto doc = std::make_unique<mulan::Document>(title.toStdString());
    auto session = std::make_unique<mulan::editor::DocumentSession>(std::move(doc));
    mulan::view::ViewConfig viewConfig;
    EngineSettings::instance().applyTo(viewConfig);
    DocumentViewport* viewport = document_workspace_->addDocument(std::move(session), title, viewConfig);
    if (!viewport) {
        LOG_ERROR("[App] New document view initialization failed: title={}", title.toStdString());
        statusBar()->showMessage(tr("Failed to initialize document view"));
        return;
    }
    LOG_INFO("[App] New document created: {}", title.toStdString());

    updateDisplayActions();
    statusBar()->showMessage(QString("Created: %1").arg(title));
}

void MainWindow::onOpenFile() {
    auto exts = document_open_service_.supportedExtensions();
    QString filter = "Model Files (";
    for (const auto& ext : exts) {
        filter += QString(" *.%1").arg(QString::fromStdString(ext));
    }
    filter += " )";

    const QStringList filePaths = QFileDialog::getOpenFileNames(this, "Open Model Files", {}, filter);
    if (filePaths.isEmpty())
        return;

    for (const QString& filePath : filePaths)
        openFilePath(filePath);
}

bool MainWindow::openFilePath(const QString& filePath, bool recordRecent) {
    MULAN_PROFILE_ZONE();

    LOG_INFO("[App] Opening document: {}", filePath.toStdString());
    mulan::view::ViewConfig viewConfig;
    EngineSettings::instance().applyTo(viewConfig);

    const DocumentOpenService::RequestId requestId = document_open_service_.openFile(
            filePath, [this, recordRecent](DocumentOpenService::RequestId completedRequest, QString completedPath,
                                           mulan::Result<mulan::io::OpenDocumentResult> opened) mutable {
                completeFileOpen(completedRequest, std::move(completedPath), recordRecent, std::move(opened));
            });
    if (requestId == 0) {
        return false;
    }

    const QString title = QFileInfo(filePath).fileName();
    if (!document_workspace_->beginDocumentOpen(requestId, title, viewConfig)) {
        LOG_ERROR("[App] Loading document view initialization failed: path={}", filePath.toStdString());
        QMessageBox::warning(this, tr("Rendering Error"), tr("Failed to initialize the document rendering view."));
        statusBar()->showMessage("Ready");
        return false;
    }

    statusBar()->showMessage("Loading: " + filePath);
    return true;
}

void MainWindow::completeFileOpen(DocumentOpenService::RequestId requestId, QString filePath, bool recordRecent,
                                  mulan::Result<mulan::io::OpenDocumentResult> opened) {
    MULAN_PROFILE_ZONE();

    if (!opened) {
        if (!document_workspace_->failDocumentOpen(requestId)) {
            LOG_DEBUG("[App] Discarded completed import for a closed loading page: request={}, path={}", requestId,
                      filePath.toStdString());
            return;
        }
        LOG_ERROR("[App] Document open failed: path={}, error={}", filePath.toStdString(), opened.error().message);
        QMessageBox::warning(this, "Import Error", QString::fromStdString(opened.error().message));
        statusBar()->showMessage("Ready");
        updateDisplayActions();
        return;
    }

    logImportReport(opened->import.report);
    auto document = std::move(opened->document);
    const QString title = QString::fromStdString(document->displayName());
    auto session = std::make_unique<mulan::editor::DocumentSession>(std::move(document),
                                                                    importedSessionOptions(opened->import.report));
    DocumentViewport* viewport = document_workspace_->completeDocumentOpen(requestId, std::move(session), title);
    if (!viewport) {
        LOG_DEBUG("[App] Discarded completed import for a closed loading page: request={}, path={}", requestId,
                  filePath.toStdString());
        return;
    }

    if (recordRecent) {
        document_workspace_->recordOpenedFile(filePath);
    }
    LOG_INFO("[App] Document opened: title={}", title.toStdString());
    LOG_DEBUG("[App] Document source: {}", filePath.toStdString());

    updateDisplayActions();
    statusBar()->showMessage(QString("Loaded: %1").arg(title));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (urls.isEmpty())
        return;

    for (const QUrl& url : urls) {
        const QString filePath = url.toLocalFile();
        if (!filePath.isEmpty())
            openFilePath(filePath);
    }
}
