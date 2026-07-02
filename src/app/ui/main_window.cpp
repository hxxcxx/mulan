#include "main_window.h"
#include "document_area.h"
#include "doc_widget.h"
#include "ui_document.h"
#include "engine_settings_dialog.h"

#include <mulan/document/document.h>
#include <mulan/io/file_manager.h>
#include <mulan/core/log/log.h>
#include <mulan/render_scene/render_scene.h>
#include <mulan/world/world.h>

#include <QFileDialog>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QFileInfo>
#include <QMenu>
#include <QActionGroup>

#include <sstream>

namespace {

void logSceneMirrorStats(const mulan::document::Document& doc) {
    auto stats = doc.sceneMirrorStats();
    std::ostringstream os;
    os << "Document mirror "
       << (stats.consistent() ? "ok" : "mismatch")
       << ": world=" << stats.worldEntityCount
       << ", scene=" << stats.sceneEntityCount
       << ", assets=" << stats.assetCount
       << ", brep=" << stats.brepAssetCount;

    if (stats.consistent()) {
        mulan::core::log::log(mulan::core::log::Level::Info, os.str());
    } else {
        mulan::core::log::log(mulan::core::log::Level::Warn, os.str());
    }
}

void logRenderSceneSyncStats(const mulan::document::Document& doc) {
    if (!doc.scene() || !doc.assets())
        return;

    size_t worldGeometryCount = 0;
    if (const auto* world = doc.world()) {
        world->forEachEntity([&](const mulan::world::Entity* entity) {
            if (entity && entity->hasGeometry())
                ++worldGeometryCount;
        });
    }

    mulan::render_scene::RenderScene renderScene;
    renderScene.sync(*doc.scene(), *doc.assets());

    const auto& stats = renderScene.lastSyncStats();
    const bool ok = stats.missingGeometryCount == 0
        && worldGeometryCount == stats.proxyCount;

    std::ostringstream os;
    os << "RenderScene sync "
       << (ok ? "ok" : "mismatch")
       << ": entities=" << stats.entityCount
       << ", assets=" << stats.assetCount
       << ", worldGeometry=" << worldGeometryCount
       << ", proxies=" << stats.proxyCount
       << ", visible=" << stats.visibleProxyCount
       << ", missingGeometry=" << stats.missingGeometryCount;

    if (ok) {
        mulan::core::log::log(mulan::core::log::Level::Info, os.str());
    } else {
        mulan::core::log::log(mulan::core::log::Level::Warn, os.str());
    }
}

} // namespace

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

    connect(doc_area_, &DocumentArea::currentDocumentChanged,
            this, &MainWindow::onCurrentDocumentChanged);

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
    action_open_ = new QAction(QIcon(":/app/bright/icon/open.svg"), tr("Open"), this);
    connect(action_open_, &QAction::triggered, this, &MainWindow::onOpenFile);
    panel_file_->addLargeAction(action_open_);

    category_home_->addPanel(panel_file_);

    // ── Navigation 面板 ──
    panel_view_ = new SARibbonPanel(tr("Navigation"), category_home_);
    action_fit_all_ = new QAction(QIcon(":/app/bright/icon/fitall.svg"), tr("Fit All"), this);
    action_fit_all_->setShortcut(Qt::Key_F);
    connect(action_fit_all_, &QAction::triggered, this, [this]() {
        if (auto* doc = doc_area_->currentDocWidget()) doc->fitAll();
    });
    panel_view_->addLargeAction(action_fit_all_);
    category_home_->addPanel(panel_view_);

    // ── Setting 面板 ──
    panel_setting_ = new SARibbonPanel(tr("Setting"), category_home_);
    auto* actionAbout = new QAction(QIcon(":/app/bright/icon/about.svg"), tr("About"), this);
    connect(actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About mulan"),
            tr("mulan v1.0\nA CAD geometry viewer."));
    });
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
    auto* actionWireframe = new QAction(QIcon(":/app/bright/icon/wireframe.svg"), tr("Wireframe"), this);
    actionWireframe->setCheckable(true);
    panel_display_->addLargeAction(actionWireframe);

    // 实体模式
    auto* actionShaded = new QAction(QIcon(":/app/bright/icon/shaded.svg"), tr("Shaded"), this);
    actionShaded->setCheckable(true);
    actionShaded->setChecked(true);
    panel_display_->addLargeAction(actionShaded);

    // 互斥
    auto* displayGroup = new QActionGroup(this);
    displayGroup->addAction(actionWireframe);
    displayGroup->addAction(actionShaded);

    panel_display_->addSeparator();

    // 显示坐标轴
    auto* actionAxis = new QAction(QIcon(":/app/bright/icon/axis.svg"), tr("Show Axis"), this);
    actionAxis->setCheckable(true);
    actionAxis->setChecked(true);
    panel_display_->addSmallAction(actionAxis);

    // 显示网格
    auto* actionGrid = new QAction(QIcon(":/app/bright/icon/grid.svg"), tr("Show Grid"), this);
    actionGrid->setCheckable(true);
    panel_display_->addSmallAction(actionGrid);

    category_view_->addPanel(panel_display_);
    ribbonBar()->addCategoryPage(category_view_);
}

void MainWindow::buildQuickAccessBar() {
    auto* bar = ribbonBar()->quickAccessBar();
    if (!bar) return;
    
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
}

void MainWindow::onEngineSettings() {
    EngineSettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::onOpenFile() {
    auto exts = doc_manager_.supportedExtensions();
    QString filter = "CAD Files (";
    for (const auto& ext : exts) {
        filter += QString(" *.%1").arg(QString::fromStdString(ext));
    }
    filter += " )";

    QString filePath = QFileDialog::getOpenFileName(this, "Open CAD File", {}, filter);
    if (filePath.isEmpty()) return;

    statusBar()->showMessage("Loading: " + filePath);

    auto doc = doc_manager_.openFile(filePath.toStdString());
    if (!doc) {
        QMessageBox::warning(this, "Import Error",
            QString::fromStdString(doc_manager_.lastError()));
        statusBar()->showMessage("Ready");
        return;
    }

    QString title = QString::fromStdString(doc->displayName());
    logSceneMirrorStats(*doc);
    logRenderSceneSyncStats(*doc);
    auto* uiDoc = new UIDocument(std::move(doc));
    doc_area_->addDocument(uiDoc, title);

    statusBar()->showMessage(
        QString("Loaded: %1")
            .arg(title));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (urls.isEmpty()) return;

    QString filePath = urls[0].toLocalFile();
    if (filePath.isEmpty()) return;

    statusBar()->showMessage("Loading: " + filePath);

    auto doc = doc_manager_.openFile(filePath.toStdString());
    if (!doc) {
        QMessageBox::warning(this, "Import Error",
            QString::fromStdString(doc_manager_.lastError()));
        statusBar()->showMessage("Ready");
        return;
    }

    QString title = QString::fromStdString(doc->displayName());
    logSceneMirrorStats(*doc);
    logRenderSceneSyncStats(*doc);
    auto* uiDoc = new UIDocument(std::move(doc));
    doc_area_->addDocument(uiDoc, title);

    statusBar()->showMessage(
        QString("Loaded: %1")
            .arg(title));
}
