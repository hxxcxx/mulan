#include "document_area.h"
#include "doc_widget.h"
#include "document_session.h"
#include "startup_page.h"

#include <SARibbon.h>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStackedWidget>

//===================================================
// DocumentArea
//===================================================

DocumentArea::DocumentArea(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    stack_ = new QStackedWidget(this);
    stack_->setStyleSheet("QStackedWidget { background: palette(window); border: none; }");

    // Page 0: 启动页 / 最近文件
    startup_page_ = new StartupPage(this);
    connect(startup_page_, &StartupPage::newDocumentRequested, this, &DocumentArea::startupNewRequested);
    connect(startup_page_, &StartupPage::openDocumentRequested, this, &DocumentArea::startupOpenRequested);
    connect(startup_page_, &StartupPage::recentFileRequested, this, &DocumentArea::startupRecentFileRequested);
    stack_->addWidget(startup_page_);

    // Page 1: 文档标签区。直接使用 SARibbon 的标签栏和堆叠容器，保证与 Ribbon 主题一致。
    document_page_ = new QWidget(this);
    auto* documentLayout = new QVBoxLayout(document_page_);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    documentLayout->setSpacing(0);

    document_tab_bar_ = new SARibbonTabBar(document_page_);
    document_tab_bar_->setObjectName("documentTabBar");
    document_tab_bar_->setTabsClosable(false);
    document_tab_bar_->setMovable(true);
    document_tab_bar_->setStyleSheet(R"(
        #documentTabBar QToolButton {
            background: transparent;
            border: none;
            border-radius: 10px;
            margin: 0 4px 0 2px;
            padding: 0;
        }
        #documentTabBar QToolButton:hover { background: #F2D8D5; }
        #documentTabBar QToolButton:pressed { background: #EBC4C0; }
        #documentTabBar QToolButton:focus { outline: none; }
    )");
    document_tab_bar_->hide();

    document_stack_ = new SARibbonStackedWidget(document_page_);
    document_stack_->setObjectName("documentStack");
    document_stack_->setNormalMode();

    connect(document_tab_bar_, &QTabBar::tabCloseRequested, this, &DocumentArea::onTabCloseRequested);
    connect(document_tab_bar_, &QTabBar::currentChanged, this, &DocumentArea::onCurrentTabChanged);
    connect(document_tab_bar_, &QTabBar::tabMoved, document_stack_, &SARibbonStackedWidget::moveWidget);

    documentLayout->addWidget(document_tab_bar_);
    documentLayout->addWidget(document_stack_, 1);
    stack_->addWidget(document_page_);
    stack_->setCurrentIndex(0);

    layout->addWidget(stack_);
}

DocumentArea::~DocumentArea() {
    for (auto& [w, doc] : docs_) {
        if (w) {
            w->setDocumentSession(nullptr);
        }
        delete doc;
    }
    docs_.clear();
}

DocWidget* DocumentArea::addDocument(DocumentSession* session, const QString& title) {
    auto* docWidget = new DocWidget(this);
    docs_[docWidget] = session;
    docWidget->setDocumentSession(session);
    connect(docWidget, &DocWidget::commandStateInvalidated, this, [this, docWidget]() {
        if (currentDocWidget() == docWidget) {
            emit currentDocumentCommandStateInvalidated();
        }
    });

    const int idx = document_stack_->addWidget(docWidget);
    document_tab_bar_->addTab(title);
    auto* closeButton = new QToolButton(document_tab_bar_);
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(24, 24);
    closeButton->setIcon(QIcon(":/app/icons/icon/tab-close.svg"));
    closeButton->setIconSize(QSize(20, 20));
    closeButton->setToolTip(tr("Close document"));
    connect(closeButton, &QToolButton::clicked, this, [this, docWidget]() {
        const int tabIndex = document_stack_->indexOf(docWidget);
        if (tabIndex >= 0)
            closeDocument(tabIndex);
    });
    document_tab_bar_->setTabButton(idx, QTabBar::RightSide, closeButton);
    document_tab_bar_->setCurrentIndex(idx);
    document_stack_->setCurrentIndex(idx);
    document_tab_bar_->show();
    stack_->setCurrentIndex(1);  // 切到标签区

    // Widget 已加入布局并设为当前页，此时 winId() 已就绪，安全初始化 Vulkan
    docWidget->init();

    emit documentOpened(title);
    emit currentDocumentChanged(title);
    return docWidget;
}

void DocumentArea::closeCurrentDocument() {
    const int idx = document_tab_bar_->currentIndex();
    if (idx >= 0)
        closeDocument(idx);
}

void DocumentArea::closeDocument(int index) {
    auto* w = document_stack_->widget(index);
    auto* docWidget = qobject_cast<DocWidget*>(w);
    if (!docWidget)
        return;

    // Unbind the view before deleting the session; the widget itself is deleted later.
    auto it = docs_.find(docWidget);
    if (it != docs_.end()) {
        docWidget->setDocumentSession(nullptr);
        delete it->second;
        docs_.erase(it);
    }

    document_stack_->removeWidget(docWidget);
    document_tab_bar_->removeTab(index);
    docWidget->deleteLater();

    emit documentClosed();

    if (document_stack_->count() == 0) {
        document_tab_bar_->hide();
        stack_->setCurrentIndex(0);  // 切回欢迎页
    }
}

DocWidget* DocumentArea::currentDocWidget() const {
    auto* w = document_stack_->currentWidget();
    return qobject_cast<DocWidget*>(w);
}

int DocumentArea::documentCount() const {
    return static_cast<int>(docs_.size());
}

void DocumentArea::recordOpenedFile(const QString& filePath) {
    startup_page_->recordOpenedFile(filePath);
}

void DocumentArea::removeRecentFile(const QString& filePath) {
    startup_page_->removeRecentFile(filePath);
}

void DocumentArea::onTabCloseRequested(int index) {
    closeDocument(index);
}

void DocumentArea::onCurrentTabChanged(int index) {
    if (index < 0) {
        emit currentDocumentChanged({});
        return;
    }

    document_stack_->setCurrentIndex(index);
    auto* w = document_stack_->widget(index);
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
