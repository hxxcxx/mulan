#include "document_page.h"

#include "document_viewport.h"

#include <mulan/editor/document/document_session.h>

#include <QLabel>
#include <QProgressBar>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <utility>

DocumentPage::DocumentPage(std::unique_ptr<mulan::editor::DocumentSession> session,
                           const mulan::view::ViewConfig& viewConfig, uint64_t openRequestId, QWidget* parent)
    : QWidget(parent), session_(std::move(session)), open_request_id_(openRequestId) {
    auto* layout = new QStackedLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setStackingMode(QStackedLayout::StackAll);

    viewport_ = new DocumentViewport(viewConfig, this);
    viewport_->setDocumentSession(session_.get());
    layout->addWidget(viewport_);

    if (open_request_id_ != 0) {
        loading_overlay_ = new QWidget(this);
        loading_overlay_->setObjectName("documentLoadingOverlay");
        loading_overlay_->setAttribute(Qt::WA_StyledBackground);

        auto* loadingLayout = new QVBoxLayout(loading_overlay_);
        loadingLayout->setContentsMargins(32, 32, 32, 32);
        loadingLayout->setAlignment(Qt::AlignCenter);
        loadingLayout->setSpacing(12);

        auto* label = new QLabel(tr("Loading document..."), loading_overlay_);
        label->setObjectName("documentLoadingLabel");
        label->setAlignment(Qt::AlignCenter);

        auto* progress = new QProgressBar(loading_overlay_);
        progress->setObjectName("documentLoadingProgress");
        progress->setRange(0, 0);
        progress->setTextVisible(false);
        progress->setMinimumWidth(220);
        progress->setMaximumWidth(320);
        progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        loadingLayout->addWidget(label, 0, Qt::AlignCenter);
        loadingLayout->addWidget(progress, 0, Qt::AlignCenter);
        layout->addWidget(loading_overlay_);
        layout->setCurrentWidget(loading_overlay_);
    }
}

DocumentPage::~DocumentPage() {
    shutdown();
}

bool DocumentPage::init() {
    return viewport_ && viewport_->init();
}

bool DocumentPage::completeOpen(uint64_t requestId, std::unique_ptr<mulan::editor::DocumentSession> session) {
    if (shutdown_ || !viewport_ || !session || requestId == 0 || requestId != open_request_id_) {
        return false;
    }

    session_ = std::move(session);
    open_request_id_ = 0;
    viewport_->setDocumentSession(session_.get());
    if (loading_overlay_) {
        loading_overlay_->hide();
        loading_overlay_->deleteLater();
        loading_overlay_ = nullptr;
    }
    return true;
}

void DocumentPage::shutdown() {
    if (shutdown_) {
        return;
    }
    shutdown_ = true;
    if (viewport_) {
        viewport_->shutdown();
    }
}
