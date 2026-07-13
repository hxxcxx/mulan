#include "main_window.h"
#include "recent_thumbnail_spec.h"
#include "document_area.h"
#include "doc_widget.h"
#include "engine_settings_dialog.h"
#include <mulan/editor/document/document_session.h>
#include <mulan/editor/command/builtin_commands.h>

#include <mulan/core/log/log.h>
#include <mulan/io/file_manager.h>
#include <mulan/io/import_result.h>
#include <mulan/view/capture/capture_image_encoder.h>
#include <mulan/view/core/view_context.h>

#include <QFileDialog>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QFileInfo>
#include <QMenu>
#include <QActionGroup>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QToolButton>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
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

QImage frameThumbnailToContent(const QImage& image) {
    if (image.isNull() || image.width() < 2 || image.height() < 2)
        return image;

    const QColor background = image.pixelColor(0, 0);
    QRect contentBounds;
    constexpr int kBackgroundTolerance = 20;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int difference =
                    std::max({ std::abs(pixel.red() - background.red()), std::abs(pixel.green() - background.green()),
                               std::abs(pixel.blue() - background.blue()) });
            if (pixel.alpha() > 0 && difference > kBackgroundTolerance) {
                contentBounds |= QRect(x, y, 1, 1);
            }
        }
    }
    if (contentBounds.isNull())
        return image;

    const int padding = std::max(8, std::min(image.width(), image.height()) / 14);
    contentBounds.adjust(-padding, -padding, padding, padding);
    contentBounds = contentBounds.intersected(image.rect());

    const QImage cropped = image.copy(contentBounds);
    QImage framed(image.size(), image.format());
    framed.fill(background);

    QPainter painter(&framed);
    const QSize targetSize = cropped.size().scaled(framed.size(), Qt::KeepAspectRatio);
    const QRect targetRect((framed.width() - targetSize.width()) / 2, (framed.height() - targetSize.height()) / 2,
                           targetSize.width(), targetSize.height());
    painter.drawImage(targetRect, cropped);
    return framed;
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
    doc_area_ = new DocumentArea(this);
    setCentralWidget(doc_area_);
    mulan::editor::registerBuiltinCommands(command_manager_);

    connect(doc_area_, &DocumentArea::currentDocumentChanged, this, &MainWindow::onCurrentDocumentChanged);
    connect(doc_area_, &DocumentArea::currentDocumentCommandStateInvalidated, this, &MainWindow::updateDisplayActions);
    connect(doc_area_, &DocumentArea::documentClosing, this,
            [this](DocWidget* document, const QString& filePath) { captureRecentThumbnail(document, filePath); });
    connect(doc_area_, &DocumentArea::startupNewRequested, this, &MainWindow::onNewDocument);
    connect(doc_area_, &DocumentArea::startupOpenRequested, this, &MainWindow::onOpenFile);
    connect(doc_area_, &DocumentArea::startupRecentFileRequested, this,
            [this](const QString& path) { openFilePath(path); });

    // 构建 Ribbon
    buildRibbon();

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() = default;

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
    category_home_->addPanel(panel_view_);

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

    action_surface_solid_ = new QAction(QIcon(":/app/icons/icon/surface-solid.svg"), tr("Solid"), this);
    action_surface_solid_->setCheckable(true);
    connect(action_surface_solid_, &QAction::triggered, this,
            [this]() { setCurrentSurfaceShading(mulan::view::SurfaceShading::SolidLit); });
    panel_display_->addSmallAction(action_surface_solid_);

    action_surface_material_ = new QAction(QIcon(":/app/icons/icon/surface-material.svg"), tr("Material"), this);
    action_surface_material_->setCheckable(true);
    connect(action_surface_material_, &QAction::triggered, this,
            [this]() { setCurrentSurfaceShading(mulan::view::SurfaceShading::SurfacePBR); });
    panel_display_->addSmallAction(action_surface_material_);

    auto* surfaceGroup = new QActionGroup(this);
    surfaceGroup->addAction(action_surface_solid_);
    surfaceGroup->addAction(action_surface_material_);

    panel_display_->addSeparator();

    action_show_cube_ = new QAction(QIcon(":/app/icons/icon/view-cube.svg"), tr("Show Cube"), this);
    action_show_cube_->setCheckable(true);
    action_show_cube_->setChecked(true);
    connect(action_show_cube_, &QAction::toggled, this, [this](bool checked) {
        auto* doc = doc_area_ ? doc_area_->currentDocWidget() : nullptr;
        if (!doc) {
            updateDisplayActions();
            return;
        }

        doc->viewContext().setShowViewCube(checked);
        doc->requestFrame();
        updateDisplayActions();
    });
    panel_display_->addSmallAction(action_show_cube_);

    category_view_->addPanel(panel_display_);
    ribbonBar()->addCategoryPage(category_view_);

    updateDisplayActions();
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
    auto* doc = doc_area_ ? doc_area_->currentDocWidget() : nullptr;
    auto* view = doc ? &doc->documentView() : nullptr;
    return view ? view->commandHost() : mulan::editor::CommandHost{};
}

void MainWindow::executeCommand(std::string_view id) {
    auto* doc = doc_area_ ? doc_area_->currentDocWidget() : nullptr;
    auto* view = doc ? &doc->documentView() : nullptr;

    if (view && view->activeEditorToolId() == id) {
        view->cancelActiveEditorTool();
        updateDisplayActions();
        return;
    }

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
    auto* doc = doc_area_ ? doc_area_->currentDocWidget() : nullptr;
    if (!doc) {
        updateDisplayActions();
        return;
    }

    doc->viewContext().setRenderMode(mode);
    doc->requestFrame();
    updateDisplayActions();
}

void MainWindow::setCurrentSurfaceShading(mulan::view::SurfaceShading shading) {
    auto* doc = doc_area_ ? doc_area_->currentDocWidget() : nullptr;
    if (!doc) {
        updateDisplayActions();
        return;
    }

    doc->viewContext().setSurfaceShading(shading);
    doc->requestFrame();
    updateDisplayActions();
}

void MainWindow::updateDisplayActions() {
    auto* doc = doc_area_ ? doc_area_->currentDocWidget() : nullptr;
    const bool hasDocument = doc != nullptr;
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
    if (action_surface_solid_) {
        action_surface_solid_->setEnabled(hasDocument);
    }
    if (action_surface_material_) {
        action_surface_material_->setEnabled(hasDocument);
    }
    if (action_show_cube_) {
        action_show_cube_->setEnabled(hasDocument);
    }
    if (!doc)
        return;

    const auto mode = doc->viewContext().renderMode();
    if (action_display_wireframe_) {
        action_display_wireframe_->setChecked(mode == mulan::view::RenderMode::Wireframe);
    }
    if (action_display_shaded_) {
        action_display_shaded_->setChecked(mode == mulan::view::RenderMode::Shaded);
    }
    if (action_display_edges_) {
        action_display_edges_->setChecked(mode == mulan::view::RenderMode::ShadedWithEdges);
    }

    const auto shading = doc->viewContext().surfaceShading();
    if (action_surface_solid_) {
        action_surface_solid_->setChecked(shading == mulan::view::SurfaceShading::SolidLit);
    }
    if (action_surface_material_) {
        action_surface_material_->setChecked(shading == mulan::view::SurfaceShading::SurfacePBR);
    }
    if (action_show_cube_) {
        const QSignalBlocker blocker(action_show_cube_);
        action_show_cube_->setChecked(doc->viewContext().showViewCube());
    }
}

void MainWindow::onEngineSettings() {
    EngineSettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::onNewDocument() {
    static int untitledIndex = 1;

    const QString title = tr("Untitled %1").arg(untitledIndex++);
    auto doc = std::make_unique<mulan::io::Document>(title.toStdString());
    auto* session = new DocumentSession(std::move(doc));
    doc_area_->addDocument(session, title);
    LOG_INFO("[App] New document created: {}", title.toStdString());

    updateDisplayActions();
    statusBar()->showMessage(QString("Created: %1").arg(title));
}

void MainWindow::onOpenFile() {
    auto exts = doc_manager_.supportedExtensions();
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
    LOG_INFO("[App] Opening document: {}", filePath.toStdString());
    statusBar()->showMessage("Loading: " + filePath);

    auto opened = doc_manager_.openFile(filePath.toStdString());
    if (!opened) {
        LOG_ERROR("[App] Document open failed: path={}, error={}", filePath.toStdString(), opened.error().message);
        QMessageBox::warning(this, "Import Error", QString::fromStdString(opened.error().message));
        statusBar()->showMessage("Ready");
        return false;
    }

    logImportReport(opened->import.report);
    auto doc = std::move(opened->document);
    QString title = QString::fromStdString(doc->displayName());
    auto* session = new DocumentSession(std::move(doc), std::move(opened->import.report));
    DocWidget* docWidget = doc_area_->addDocument(session, title);
    if (recordRecent) {
        doc_area_->recordOpenedFile(filePath);
        scheduleRecentThumbnailCapture(docWidget, filePath);
    }
    LOG_INFO("[App] Document opened: title={}", title.toStdString());
    LOG_DEBUG("[App] Document source: {}", filePath.toStdString());

    updateDisplayActions();
    statusBar()->showMessage(QString("Loaded: %1").arg(title));
    return true;
}

void MainWindow::scheduleRecentThumbnailCapture(DocWidget* docWidget, const QString& filePath) {
    if (!docWidget || filePath.isEmpty())
        return;

    const QPointer<DocWidget> guardedWidget(docWidget);

    QTimer::singleShot(0, this, [this, guardedWidget, filePath]() {
        if (!guardedWidget)
            return;

        captureRecentThumbnail(guardedWidget, filePath);
    });
}

void MainWindow::captureRecentThumbnail(DocWidget* docWidget, const QString& filePath) {
    if (!docWidget || filePath.isEmpty())
        return;

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (cacheRoot.isEmpty())
        return;
    const QString thumbnailDirectory = QDir(cacheRoot).filePath("recent-thumbnails");
    if (!QDir().mkpath(thumbnailDirectory))
        return;

    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    const QByteArray key = QCryptographicHash::hash(absolutePath.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString thumbnailPath = QDir(thumbnailDirectory).filePath(QString::fromLatin1(key) + ".png");

    // 截图复用文档视图已有的 RHI Device。运行时持有同级离屏表面，
    // 因此既不读取 Qt 像素，也不会为缩略图创建第二个 Device 或后端。
    auto& viewContext = docWidget->viewContext();
    if (!viewContext.isInitialized())
        return;

    mulan::view::CaptureRequest request;
    request.name = "recent-thumbnail";
    request.desc.width = recent_thumbnail::kCaptureWidth;
    request.desc.height = recent_thumbnail::kCaptureHeight;
    request.desc.format = mulan::engine::TextureFormat::RGBA8_UNorm;
    request.desc.readback = true;
    request.camera = viewContext.camera();
    request.visual.style = mulan::view::CaptureRenderStyle::ShadedWithEdges;
    request.visual.showViewCube = false;
    request.visual.showOverlays = false;

    auto captured = viewContext.capture(request);
    if (!captured)
        return;
    auto saved = mulan::view::CaptureImageEncoder::savePNG(*captured, thumbnailPath.toStdString());
    if (!saved)
        return;

    const QImage framed = frameThumbnailToContent(QImage(thumbnailPath));
    if (!framed.isNull() && framed.save(thumbnailPath, "PNG")) {
        doc_area_->setRecentThumbnail(filePath, thumbnailPath);
    }
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
