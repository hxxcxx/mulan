#include "document_area.h"
#include "doc_widget.h"
#include "ui_document.h"

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

    stack_ = new QStackedWidget(this);

    // Page 0: 欢迎页
    welcome_page_ = new QLabel(
        "<h2 style='color:#888;'>mulan</h2>"
        "<p style='color:#aaa;'>Open a CAD file to begin: File → Open, or drag & drop</p>",
        this);
    welcome_page_->setAlignment(Qt::AlignCenter);
    welcome_page_->setStyleSheet("background-color: #373a45;");
    stack_->addWidget(welcome_page_);

    // Page 1: 文档标签区
    tab_widget_ = new QTabWidget(this);
    tab_widget_->setTabsClosable(true);
    tab_widget_->setMovable(true);
    tab_widget_->setDocumentMode(true);
    tab_widget_->tabBar()->hide();

    connect(tab_widget_, &QTabWidget::tabCloseRequested,
            this, &DocumentArea::onTabCloseRequested);
    connect(tab_widget_, &QTabWidget::currentChanged,
            this, &DocumentArea::onCurrentTabChanged);

    stack_->addWidget(tab_widget_);
    stack_->setCurrentIndex(0);

    layout->addWidget(stack_);
}

DocumentArea::~DocumentArea() {
    for (auto& [w, doc] : docs_) {
        delete doc;
    }
    docs_.clear();
}

DocWidget* DocumentArea::addDocument(UIDocument* uiDoc, const QString& title) {
    auto* docWidget = new DocWidget(this);
    docs_[docWidget] = uiDoc;
    docWidget->setUIDocument(uiDoc);

    int idx = tab_widget_->addTab(docWidget, title);
    tab_widget_->setCurrentIndex(idx);
    tab_widget_->tabBar()->show();
    stack_->setCurrentIndex(1);  // 切到标签区

    // Widget 已加入布局并设为当前页，此时 winId() 已就绪，安全初始化 Vulkan
    docWidget->init();

    emit documentOpened(title);
    return docWidget;
}

void DocumentArea::closeCurrentDocument() {
    int idx = tab_widget_->currentIndex();
    if (idx >= 0) closeDocument(idx);
}

void DocumentArea::closeDocument(int index) {
    auto* w = tab_widget_->widget(index);
    auto* docWidget = qobject_cast<DocWidget*>(w);
    if (!docWidget) return;

    docs_.erase(docWidget);
    tab_widget_->removeTab(index);
    docWidget->deleteLater();

    emit documentClosed();

    if (tab_widget_->count() == 0) {
        tab_widget_->tabBar()->hide();
        stack_->setCurrentIndex(0);  // 切回欢迎页
    }
}

DocWidget* DocumentArea::currentDocWidget() const {
    auto* w = tab_widget_->currentWidget();
    return qobject_cast<DocWidget*>(w);
}

int DocumentArea::documentCount() const {
    return static_cast<int>(docs_.size());
}

void DocumentArea::onTabCloseRequested(int index) {
    closeDocument(index);
}

void DocumentArea::onCurrentTabChanged(int index) {
    if (index < 0) return;

    auto* w = tab_widget_->widget(index);
    auto* docWidget = qobject_cast<DocWidget*>(w);
    if (!docWidget) {
        emit currentDocumentChanged({});
        return;
    }

    auto it = docs_.find(docWidget);
    if (it != docs_.end()) {
        QString name = QString::fromStdString(it->second->displayName());
        emit currentDocumentChanged(name);
    }
}
