#include "main_window.h"
#include "document_area.h"
#include "doc_widget.h"
#include "document_session.h"
#include "engine_settings_dialog.h"

#include <mulan/io/file_manager.h>
#include <mulan/io/import_result.h>
#include <mulan/core/log/log.h>

#include <QFileDialog>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QFileInfo>
#include <QMenu>
#include <QActionGroup>
#include <QSignalBlocker>

#include <memory>
#include <sstream>

namespace {

void logImportReport(const mulan::io::ImportReport& report) {
    std::ostringstream os;
    os << "Import ok"
       << ": entities=" << report.entityCount << ", mesh=" << report.meshAssetCount
       << ", brep=" << report.brepAssetCount << ", primitives=" << report.primitiveCount
       << ", materials=" << report.materialCount << ", textures=" << report.textureCount;
    mulan::core::log::log(mulan::core::log::Level::Info, os.str());

    for (const auto& warning : report.warnings) {
        mulan::core::log::log(mulan::core::log::Level::Warn, warning);
    }
}

}  // namespace

//===================================================
// MainWindow
//===================================================

MainWindow::MainWindow(QWidget* parent) : SARibbonMainWindow(parent) {
    setWindowTitle("mulan");
    resize(1280, 720);
    setAcceptDrops(true);

    // 中央多文档区
    doc_area_ = new DocumentArea(this);
    setCentralWidget(doc_area_);

    connect(doc_area_, &DocumentArea::currentDocumentChanged, this, &MainWindow::onCurrentDocumentChanged);

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

    buildRibbonHomeCategory();
    buildRibbonViewCategory();
    buildQuickAccessBar();
    buildRightButtonBar();
}

void MainWindow::buildRibbonHomeCategory() {
    category_home_ = new SARibbonCategory(this);
    category_home_->setCategoryName(tr("Home"));

    // ── File 面板 ──
    panel_file_ = new SARibbonPanel(tr("File"), category_home_);
    action_new_ = new QAction(QIcon(":/app/bright/icon/newAxis.svg"), tr("New"), this);
    action_new_->setShortcut(QKeySequence::New);
    connect(action_new_, &QAction::triggered, this, &MainWindow::onNewDocument);
    panel_file_->addLargeAction(action_new_);

    action_open_ = new QAction(QIcon(":/app/bright/icon/open.svg"), tr("Open"), this);
    connect(action_open_, &QAction::triggered, this, &MainWindow::onOpenFile);
    panel_file_->addLargeAction(action_open_);

    category_home_->addPanel(panel_file_);

    panel_draw_ = new SARibbonPanel(tr("Draw"), category_home_);
    action_draw_line_ = new QAction(QIcon(":/app/bright/icon/link.svg"), tr("Line"), this);
    connect(action_draw_line_, &QAction::triggered, this, [this]() {
        if (auto* doc = doc_area_->currentDocWidget())
            doc->startDrawLine();
    });
    panel_draw_->addLargeAction(action_draw_line_);
    category_home_->addPanel(panel_draw_);

    // ── Navigation 面板 ──
    panel_view_ = new SARibbonPanel(tr("Navigation"), category_home_);
    action_fit_all_ = new QAction(QIcon(":/app/bright/icon/fitall.svg"), tr("Fit All"), this);
    action_fit_all_->setShortcut(Qt::Key_F);
    connect(action_fit_all_, &QAction::triggered, this, [this]() {
        if (auto* doc = doc_area_->currentDocWidget())
            doc->fitAll();
    });
    panel_view_->addLargeAction(action_fit_all_);
    category_home_->addPanel(panel_view_);

    // ── Setting 面板 ──
    panel_setting_ = new SARibbonPanel(tr("Setting"), category_home_);
    auto* actionAbout = new QAction(QIcon(":/app/bright/icon/about.svg"), tr("About"), this);
    connect(actionAbout, &QAction::triggered, this,
            [this]() { QMessageBox::about(this, tr("About mulan"), tr("mulan v1.0\nA model geometry viewer.")); });
    panel_setting_->addLargeAction(actionAbout);

    action_engine_settings_ = new QAction(QIcon(":/app/bright/icon/setting.svg"), tr("Engine"), this);
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
    action_display_wireframe_ = new QAction(QIcon(":/app/bright/icon/wireframe.svg"), tr("Wireframe"), this);
    action_display_wireframe_->setCheckable(true);
    connect(action_display_wireframe_, &QAction::triggered, this,
            [this]() { setCurrentRenderMode(mulan::view::RenderMode::Wireframe); });
    panel_display_->addLargeAction(action_display_wireframe_);

    // 实体模式
    action_display_shaded_ = new QAction(QIcon(":/app/bright/icon/shaded.svg"), tr("Shaded"), this);
    action_display_shaded_->setCheckable(true);
    action_display_shaded_->setChecked(true);
    connect(action_display_shaded_, &QAction::triggered, this,
            [this]() { setCurrentRenderMode(mulan::view::RenderMode::Shaded); });
    panel_display_->addLargeAction(action_display_shaded_);

    action_display_edges_ = new QAction(QIcon(":/app/bright/icon/wireframe.svg"), tr("Edges"), this);
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

    action_surface_solid_ = new QAction(QIcon(":/app/bright/icon/shaded.svg"), tr("Solid"), this);
    action_surface_solid_->setCheckable(true);
    connect(action_surface_solid_, &QAction::triggered, this,
            [this]() { setCurrentSurfaceShading(mulan::view::SurfaceShading::SolidLit); });
    panel_display_->addSmallAction(action_surface_solid_);

    action_surface_material_ = new QAction(QIcon(":/app/bright/icon/showInfomation.svg"), tr("Material"), this);
    action_surface_material_->setCheckable(true);
    connect(action_surface_material_, &QAction::triggered, this,
            [this]() { setCurrentSurfaceShading(mulan::view::SurfaceShading::SurfacePBR); });
    panel_display_->addSmallAction(action_surface_material_);

    auto* surfaceGroup = new QActionGroup(this);
    surfaceGroup->addAction(action_surface_solid_);
    surfaceGroup->addAction(action_surface_material_);

    panel_display_->addSeparator();

    action_show_cube_ = new QAction(QIcon(":/app/bright/icon/axis.svg"), tr("Show Cube"), this);
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

    auto* actionUndo = new QAction(QIcon(":/app/bright/icon/undo.svg"), tr("Undo"), this);
    actionUndo->setShortcut(QKeySequence::Undo);
    bar->addAction(actionUndo);

    auto* actionRedo = new QAction(QIcon(":/app/bright/icon/redo.svg"), tr("Redo"), this);
    actionRedo->setShortcut(QKeySequence::Redo);
    bar->addAction(actionRedo);
}

void MainWindow::buildRightButtonBar() {
    // 暂无右侧按钮栏
}

//===================================================
// 文档操作
//===================================================

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
    if (action_draw_line_) {
        action_draw_line_->setEnabled(hasDocument);
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

    statusBar()->showMessage(QString("Created: %1").arg(title));
}

void MainWindow::onOpenFile() {
    auto exts = doc_manager_.supportedExtensions();
    QString filter = "Model Files (";
    for (const auto& ext : exts) {
        filter += QString(" *.%1").arg(QString::fromStdString(ext));
    }
    filter += " )";

    QString filePath = QFileDialog::getOpenFileName(this, "Open Model File", {}, filter);
    if (filePath.isEmpty())
        return;

    statusBar()->showMessage("Loading: " + filePath);

    auto opened = doc_manager_.openFile(filePath.toStdString());
    if (!opened) {
        QMessageBox::warning(this, "Import Error", QString::fromStdString(opened.error().message));
        statusBar()->showMessage("Ready");
        return;
    }

    logImportReport(opened->import.report);
    auto doc = std::move(opened->document);
    QString title = QString::fromStdString(doc->displayName());
    auto* session = new DocumentSession(std::move(doc), std::move(opened->import.report));
    doc_area_->addDocument(session, title);

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

    QString filePath = urls[0].toLocalFile();
    if (filePath.isEmpty())
        return;

    statusBar()->showMessage("Loading: " + filePath);

    auto opened = doc_manager_.openFile(filePath.toStdString());
    if (!opened) {
        QMessageBox::warning(this, "Import Error", QString::fromStdString(opened.error().message));
        statusBar()->showMessage("Ready");
        return;
    }

    logImportReport(opened->import.report);
    auto doc = std::move(opened->document);
    QString title = QString::fromStdString(doc->displayName());
    auto* session = new DocumentSession(std::move(doc), std::move(opened->import.report));
    doc_area_->addDocument(session, title);

    statusBar()->showMessage(QString("Loaded: %1").arg(title));
}
