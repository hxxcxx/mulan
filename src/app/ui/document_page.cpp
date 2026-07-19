#include "document_page.h"

#include "document_viewport.h"

#include <mulan/editor/document/document_session.h>

#include <QVBoxLayout>

#include <utility>

DocumentPage::DocumentPage(std::unique_ptr<mulan::editor::DocumentSession> session,
                           const mulan::view::ViewConfig& viewConfig, QWidget* parent)
    : QWidget(parent), session_(std::move(session)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    viewport_ = new DocumentViewport(viewConfig, this);
    viewport_->setDocumentSession(session_.get());
    layout->addWidget(viewport_);
}

DocumentPage::~DocumentPage() {
    shutdown();
}

bool DocumentPage::init() {
    return session_ && viewport_ && viewport_->init();
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
