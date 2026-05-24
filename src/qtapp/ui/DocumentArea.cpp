/**
 * @file DocumentArea.cpp
 * @brief 多文档标签管理区实现
 * @author hxxcxx
 * @date 2026-04-23
 */
#include "DocumentArea.h"
#include "DocWidget.h"
#include "UIDocument.h"

#include <QLabel>
#include <QTabBar>
#include <QVBoxLayout>
#include <QStackedWidget>

//===================================================
// DocumentArea
//===================================================

DocumentArea::DocumentArea(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);

    // Page 0: 欢迎页
    m_welcomePage = new QLabel(
        "<h2 style='color:#888;'>MulanGeo</h2>"
        "<p style='color:#aaa;'>Open a CAD file to begin: File → Open, or drag & drop</p>",
        this);
    m_welcomePage->setAlignment(Qt::AlignCenter);
    m_welcomePage->setStyleSheet("background-color: #373a45;");
    m_stack->addWidget(m_welcomePage);

    // Page 1: 文档标签区
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->tabBar()->hide();

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &DocumentArea::onTabCloseRequested);
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &DocumentArea::onCurrentTabChanged);

    m_stack->addWidget(m_tabWidget);
    m_stack->setCurrentIndex(0);

    layout->addWidget(m_stack);
}

DocumentArea::~DocumentArea() {
    for (auto& [w, doc] : m_docs) {
        delete doc;
    }
    m_docs.clear();
}

DocWidget* DocumentArea::addDocument(UIDocument* uiDoc, const QString& title) {
    auto* docWidget = new DocWidget(this);
    m_docs[docWidget] = uiDoc;
    docWidget->setUIDocument(uiDoc);

    int idx = m_tabWidget->addTab(docWidget, title);
    m_tabWidget->setCurrentIndex(idx);
    m_tabWidget->tabBar()->show();
    m_stack->setCurrentIndex(1);  // 切到标签区

    // Widget 已加入布局并设为当前页，此时 winId() 已就绪，安全初始化 Vulkan
    docWidget->init();

    emit documentOpened(title);
    return docWidget;
}

void DocumentArea::closeCurrentDocument() {
    int idx = m_tabWidget->currentIndex();
    if (idx >= 0) closeDocument(idx);
}

void DocumentArea::closeDocument(int index) {
    auto* w = m_tabWidget->widget(index);
    auto* docWidget = qobject_cast<DocWidget*>(w);
    if (!docWidget) return;

    m_docs.erase(docWidget);
    m_tabWidget->removeTab(index);
    docWidget->deleteLater();

    emit documentClosed();

    if (m_tabWidget->count() == 0) {
        m_tabWidget->tabBar()->hide();
        m_stack->setCurrentIndex(0);  // 切回欢迎页
    }
}

DocWidget* DocumentArea::currentDocWidget() const {
    auto* w = m_tabWidget->currentWidget();
    return qobject_cast<DocWidget*>(w);
}

int DocumentArea::documentCount() const {
    return static_cast<int>(m_docs.size());
}

void DocumentArea::onTabCloseRequested(int index) {
    closeDocument(index);
}

void DocumentArea::onCurrentTabChanged(int index) {
    if (index < 0) return;

    auto* w = m_tabWidget->widget(index);
    auto* docWidget = qobject_cast<DocWidget*>(w);
    if (!docWidget) {
        emit currentDocumentChanged({});
        return;
    }

    auto it = m_docs.find(docWidget);
    if (it != m_docs.end()) {
        QString name = QString::fromStdString(it->second->document().displayName());
        emit currentDocumentChanged(name);
    }
}
