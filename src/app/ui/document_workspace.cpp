#include "document_workspace.h"

#include <mulan/core/profiling/profile.h>
#include "document_page.h"
#include "document_viewport.h"
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
// DocumentWorkspace
//===================================================

DocumentWorkspace::DocumentWorkspace(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName("documentAreaStack");

    // Page 0: 启动页 / 最近文件
    startup_page_ = new StartupPage(this);
    connect(startup_page_, &StartupPage::newDocumentRequested, this, &DocumentWorkspace::startupNewRequested);
    connect(startup_page_, &StartupPage::openDocumentRequested, this, &DocumentWorkspace::startupOpenRequested);
    connect(startup_page_, &StartupPage::recentFileRequested, this, &DocumentWorkspace::startupRecentFileRequested);
    connect(startup_page_, &StartupPage::recentFileMissing, this, [this](const QString& filePath) {
        recent_files_store_.removeFile(filePath);
        refreshRecentFiles();
    });
    refreshRecentFiles();
    stack_->addWidget(startup_page_);

    // Page 1: 文档标签区。直接使用 SARibbon 的标签栏和堆叠容器，保证与 Ribbon 主题一致。
    documents_page_ = new QWidget(this);
    auto* documentLayout = new QVBoxLayout(documents_page_);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    documentLayout->setSpacing(0);

    document_tab_bar_ = new SARibbonTabBar(documents_page_);
    document_tab_bar_->setObjectName("documentTabBar");
    document_tab_bar_->setTabsClosable(false);
    document_tab_bar_->setMovable(true);
    document_tab_bar_->hide();

    document_stack_ = new SARibbonStackedWidget(documents_page_);
    document_stack_->setObjectName("documentStack");
    document_stack_->setNormalMode();

    connect(document_tab_bar_, &QTabBar::tabCloseRequested, this, &DocumentWorkspace::onTabCloseRequested);
    connect(document_tab_bar_, &QTabBar::currentChanged, this, &DocumentWorkspace::onCurrentTabChanged);
    connect(document_tab_bar_, &QTabBar::tabMoved, document_stack_, &SARibbonStackedWidget::moveWidget);

    documentLayout->addWidget(document_tab_bar_);
    documentLayout->addWidget(document_stack_, 1);
    stack_->addWidget(documents_page_);
    stack_->setCurrentIndex(0);

    layout->addWidget(stack_);
}

DocumentWorkspace::~DocumentWorkspace() {
    for (int index = 0; index < document_stack_->count(); ++index) {
        if (DocumentPage* page = pageAt(index)) {
            page->shutdown();
        }
    }
}

DocumentViewport* DocumentWorkspace::addDocument(std::unique_ptr<mulan::editor::DocumentSession> session,
                                                 const QString& title, const mulan::view::ViewConfig& viewConfig) {
    MULAN_PROFILE_ZONE();

    if (!session) {
        return nullptr;
    }

    auto pendingPage = std::make_unique<DocumentPage>(std::move(session), viewConfig, this);
    DocumentPage* page = pendingPage.get();
    DocumentViewport* viewport = page->viewport();
    connect(viewport, &DocumentViewport::commandStateInvalidated, this, [this, viewport]() {
        if (currentViewport() == viewport) {
            emit currentDocumentCommandStateInvalidated();
        }
    });
    connect(viewport, &DocumentViewport::runtimeFailed, this, [this, viewport](const QString& message) {
        if (currentViewport() == viewport) {
            emit currentDocumentRuntimeFailed(message);
        }
    });

    const int idx = document_stack_->addWidget(page);
    document_tab_bar_->addTab(title);
    auto* closeButton = new QToolButton(document_tab_bar_);
    closeButton->setProperty("uiRole", "documentClose");
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(24, 24);
    closeButton->setToolTip(tr("Close document"));
    connect(closeButton, &QToolButton::clicked, this, [this, page]() {
        const int tabIndex = document_stack_->indexOf(page);
        if (tabIndex >= 0)
            closeDocument(tabIndex);
    });
    document_tab_bar_->setTabButton(idx, QTabBar::RightSide, closeButton);
    document_tab_bar_->setCurrentIndex(idx);
    document_stack_->setCurrentIndex(idx);
    document_tab_bar_->show();
    stack_->setCurrentIndex(1);  // 切到标签区

    // Widget 已加入布局并设为当前页，此时 winId() 已就绪，安全初始化 Vulkan
    if (!page->init()) {
        page->shutdown();
        document_stack_->removeWidget(page);
        document_tab_bar_->removeTab(idx);
        if (document_stack_->count() == 0) {
            document_tab_bar_->hide();
            stack_->setCurrentIndex(0);
        }
        return nullptr;
    }

    pendingPage.release();
    emit currentDocumentChanged(title);
    return viewport;
}

bool DocumentWorkspace::closeDocument(int index) {
    if (!confirmDiscard(index)) {
        return false;
    }
    return closeDocumentUnchecked(index);
}

bool DocumentWorkspace::closeAllDocuments() {
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

bool DocumentWorkspace::confirmDiscard(int index) {
    DocumentPage* page = pageAt(index);
    if (!page || !page->session()) {
        return true;
    }

    if (!page->session()->requiresDiscardConfirmation()) {
        return true;
    }

    const QString displayName = QString::fromStdString(page->session()->displayName());
    const auto choice = QMessageBox::warning(
            this, tr("Discard unsaved changes?"),
            tr("\"%1\" has unsaved changes. Closing it will discard those changes.").arg(displayName),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return choice == QMessageBox::Discard;
}

bool DocumentWorkspace::closeDocumentUnchecked(int index) {
    DocumentPage* page = pageAt(index);
    if (!page || !page->session() || !page->viewport())
        return false;

    const auto* document = page->session()->document();
    const QString filePath = document ? QString::fromStdString(document->filePath()) : QString{};
    if (!filePath.isEmpty()) {
        emit documentClosing(page->viewport(), filePath);
    }
    page->shutdown();

    document_stack_->removeWidget(page);
    document_tab_bar_->removeTab(index);
    page->deleteLater();

    if (document_stack_->count() == 0) {
        document_tab_bar_->hide();
        stack_->setCurrentIndex(0);  // 切回欢迎页
    }
    return true;
}

DocumentViewport* DocumentWorkspace::currentViewport() const {
    DocumentPage* page = pageAt(document_stack_->currentIndex());
    return page ? page->viewport() : nullptr;
}

void DocumentWorkspace::recordOpenedFile(const QString& filePath) {
    recent_files_store_.recordOpenedFile(filePath);
    refreshRecentFiles();
}

void DocumentWorkspace::setRecentThumbnail(const QString& filePath, const QString& thumbnailPath) {
    recent_files_store_.setThumbnail(filePath, thumbnailPath);
    refreshRecentFiles();
}

void DocumentWorkspace::onTabCloseRequested(int index) {
    closeDocument(index);
}

void DocumentWorkspace::onCurrentTabChanged(int index) {
    if (index < 0) {
        emit currentDocumentChanged({});
        return;
    }

    document_stack_->setCurrentIndex(index);
    DocumentPage* page = pageAt(index);
    if (!page || !page->session()) {
        emit currentDocumentChanged({});
        return;
    }

    emit currentDocumentChanged(QString::fromStdString(page->session()->displayName()));
}

DocumentPage* DocumentWorkspace::pageAt(int index) const {
    return index >= 0 ? qobject_cast<DocumentPage*>(document_stack_->widget(index)) : nullptr;
}

void DocumentWorkspace::refreshRecentFiles() {
    startup_page_->setRecentFiles(recent_files_store_.entries());
}
