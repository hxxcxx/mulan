#include "document_area.h"
#include "doc_widget.h"
#include <mulan/editor/document/document_session.h>
#include "startup_page.h"

#include <SARibbon.h>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QMessageBox>

#include <memory>
#include <utility>

//===================================================
// DocumentArea
//===================================================

DocumentArea::DocumentArea(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName("documentAreaStack");

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
    // 先让全部视图停止并解除借用，再统一销毁会话。
    for (const auto& entry : docs_) {
        if (entry.first) {
            entry.first->shutdown();
        }
    }
    docs_.clear();
}

DocWidget* DocumentArea::addDocument(std::unique_ptr<DocumentSession> session, const QString& title) {
    if (!session) {
        return nullptr;
    }

    auto pendingWidget = std::make_unique<DocWidget>(this);
    auto* docWidget = pendingWidget.get();
    const auto [sessionIt, inserted] = docs_.emplace(docWidget, std::move(session));
    if (!inserted) {
        return nullptr;
    }
    docWidget->setDocumentSession(sessionIt->second.get());
    connect(docWidget, &DocWidget::commandStateInvalidated, this, [this, docWidget]() {
        if (currentDocWidget() == docWidget) {
            emit currentDocumentCommandStateInvalidated();
        }
    });

    const int idx = document_stack_->addWidget(docWidget);
    document_tab_bar_->addTab(title);
    auto* closeButton = new QToolButton(document_tab_bar_);
    closeButton->setProperty("uiRole", "documentClose");
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(24, 24);
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
    if (!docWidget->init()) {
        docWidget->shutdown();
        docs_.erase(docWidget);
        document_stack_->removeWidget(docWidget);
        document_tab_bar_->removeTab(idx);
        if (document_stack_->count() == 0) {
            document_tab_bar_->hide();
            stack_->setCurrentIndex(0);
        }
        return nullptr;
    }

    pendingWidget.release();
    emit documentOpened(title);
    emit currentDocumentChanged(title);
    return docWidget;
}

bool DocumentArea::closeCurrentDocument() {
    const int idx = document_tab_bar_->currentIndex();
    return idx < 0 || closeDocument(idx);
}

bool DocumentArea::closeDocument(int index) {
    if (!confirmDiscard(index)) {
        return false;
    }
    return closeDocumentUnchecked(index);
}

bool DocumentArea::closeAllDocuments() {
    // 先完成全部确认，再开始销毁。否则用户在后一个文档选择取消时，前面的文档已经无法恢复。
    for (int index = 0; index < document_stack_->count(); ++index) {
        if (!confirmDiscard(index)) {
            return false;
        }
    }

    while (document_stack_->count() > 0) {
        if (!closeDocumentUnchecked(document_stack_->count() - 1)) {
            return false;
        }
    }
    return true;
}

bool DocumentArea::confirmDiscard(int index) {
    auto* docWidget = qobject_cast<DocWidget*>(document_stack_->widget(index));
    if (!docWidget) {
        return true;
    }

    const auto it = docs_.find(docWidget);
    if (it == docs_.end() || !it->second || !it->second->requiresDiscardConfirmation()) {
        return true;
    }

    const QString displayName = QString::fromStdString(it->second->displayName());
    const auto choice = QMessageBox::warning(
            this, tr("Discard unsaved changes?"),
            tr("\"%1\" has unsaved changes. Closing it will discard those changes.").arg(displayName),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return choice == QMessageBox::Discard;
}

bool DocumentArea::closeDocumentUnchecked(int index) {
    auto* w = document_stack_->widget(index);
    auto* docWidget = qobject_cast<DocWidget*>(w);
    if (!docWidget)
        return false;

    // 会话销毁前先停止视图并解除借用；控件交给 Qt 事件循环延迟销毁。
    auto it = docs_.find(docWidget);
    if (it != docs_.end()) {
        const auto* document = it->second ? it->second->document() : nullptr;
        const QString filePath = document ? QString::fromStdString(document->filePath()) : QString{};
        if (!filePath.isEmpty()) {
            emit documentClosing(docWidget, filePath);
        }
        docWidget->shutdown();
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
    return true;
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

void DocumentArea::setRecentThumbnail(const QString& filePath, const QString& thumbnailPath) {
    startup_page_->setRecentThumbnail(filePath, thumbnailPath);
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
