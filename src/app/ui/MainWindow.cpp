/**
 * @file MainWindow.cpp
 * @brief Qt 主窗口实现 — SARibbon + DocumentArea
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "MainWindow.h"
#include "DocumentArea.h"
#include "DocWidget.h"
#include "UIDocument.h"
#include "EngineSettingsDialog.h"

#include <mulan/io/FileManager.h>

#include <QFileDialog>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QFileInfo>
#include <QMenu>
#include <QActionGroup>

//===================================================
// MainWindow
//===================================================

MainWindow::MainWindow(QWidget* parent) : SARibbonMainWindow(parent) {
    setWindowTitle("mulan");
    resize(1280, 720);
    setAcceptDrops(true);

    // 中央多文档区
    m_docArea = new DocumentArea(this);
    setCentralWidget(m_docArea);

    connect(m_docArea, &DocumentArea::currentDocumentChanged,
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
    m_categoryHome = new SARibbonCategory(this);
    m_categoryHome->setCategoryName(tr("Home"));

    // ── File 面板 ──
    m_panelFile = new SARibbonPanel(tr("File"), m_categoryHome);
    m_actionOpen = new QAction(QIcon(":/app/bright/icon/open.svg"), tr("Open"), this);
    connect(m_actionOpen, &QAction::triggered, this, &MainWindow::onOpenFile);
    m_panelFile->addLargeAction(m_actionOpen);

    m_categoryHome->addPanel(m_panelFile);

    // ── Navigation 面板 ──
    m_panelView = new SARibbonPanel(tr("Navigation"), m_categoryHome);
    m_actionFitAll = new QAction(QIcon(":/app/bright/icon/fitall.svg"), tr("Fit All"), this);
    m_actionFitAll->setShortcut(Qt::Key_F);
    connect(m_actionFitAll, &QAction::triggered, this, [this]() {
        if (auto* doc = m_docArea->currentDocWidget()) doc->fitAll();
    });
    m_panelView->addLargeAction(m_actionFitAll);
    m_categoryHome->addPanel(m_panelView);

    // ── Setting 面板 ──
    m_panelSetting = new SARibbonPanel(tr("Setting"), m_categoryHome);
    auto* actionAbout = new QAction(QIcon(":/app/bright/icon/about.svg"), tr("About"), this);
    connect(actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About mulan"),
            tr("mulan v1.0\nA CAD geometry viewer."));
    });
    m_panelSetting->addLargeAction(actionAbout);

    m_actionEngineSettings = new QAction(QIcon(":/app/bright/icon/setting.svg"), tr("Engine"), this);
    connect(m_actionEngineSettings, &QAction::triggered, this, &MainWindow::onEngineSettings);
    m_panelSetting->addLargeAction(m_actionEngineSettings);

    m_categoryHome->addPanel(m_panelSetting);

    ribbonBar()->addCategoryPage(m_categoryHome);
}

void MainWindow::buildRibbonViewCategory() {
    m_categoryView = new SARibbonCategory(this);
    m_categoryView->setCategoryName(tr("View"));

    m_panelDisplay = new SARibbonPanel(tr("Display"), m_categoryView);

    // 线框模式
    auto* actionWireframe = new QAction(QIcon(":/app/bright/icon/wireframe.svg"), tr("Wireframe"), this);
    actionWireframe->setCheckable(true);
    m_panelDisplay->addLargeAction(actionWireframe);

    // 实体模式
    auto* actionShaded = new QAction(QIcon(":/app/bright/icon/shaded.svg"), tr("Shaded"), this);
    actionShaded->setCheckable(true);
    actionShaded->setChecked(true);
    m_panelDisplay->addLargeAction(actionShaded);

    // 互斥
    auto* displayGroup = new QActionGroup(this);
    displayGroup->addAction(actionWireframe);
    displayGroup->addAction(actionShaded);

    m_panelDisplay->addSeparator();

    // 显示坐标轴
    auto* actionAxis = new QAction(QIcon(":/app/bright/icon/axis.svg"), tr("Show Axis"), this);
    actionAxis->setCheckable(true);
    actionAxis->setChecked(true);
    m_panelDisplay->addSmallAction(actionAxis);

    // 显示网格
    auto* actionGrid = new QAction(QIcon(":/app/bright/icon/grid.svg"), tr("Show Grid"), this);
    actionGrid->setCheckable(true);
    m_panelDisplay->addSmallAction(actionGrid);

    m_categoryView->addPanel(m_panelDisplay);
    ribbonBar()->addCategoryPage(m_categoryView);
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
    auto exts = m_docManager.supportedExtensions();
    QString filter = "CAD Files (";
    for (const auto& ext : exts) {
        filter += QString(" *.%1").arg(QString::fromStdString(ext));
    }
    filter += " )";

    QString filePath = QFileDialog::getOpenFileName(this, "Open CAD File", {}, filter);
    if (filePath.isEmpty()) return;

    statusBar()->showMessage("Loading: " + filePath);

    auto world = m_docManager.openFile(filePath.toStdString());
    if (!world) {
        QMessageBox::warning(this, "Import Error",
            QString::fromStdString(m_docManager.lastError()));
        statusBar()->showMessage("Ready");
        return;
    }

    QString title = QFileInfo(filePath).fileName();
    auto* uiDoc = new UIDocument(std::move(world), title.toStdString());
    m_docArea->addDocument(uiDoc, title);

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

    auto world = m_docManager.openFile(filePath.toStdString());
    if (!world) {
        QMessageBox::warning(this, "Import Error",
            QString::fromStdString(m_docManager.lastError()));
        statusBar()->showMessage("Ready");
        return;
    }

    QString title = QFileInfo(filePath).fileName();
    auto* uiDoc = new UIDocument(std::move(world), title.toStdString());
    m_docArea->addDocument(uiDoc, title);

    statusBar()->showMessage(
        QString("Loaded: %1")
            .arg(title));
}