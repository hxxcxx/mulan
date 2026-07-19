/**
 * @file document_page.h
 * @brief 将一个 DocumentSession 与其 DocumentViewport 绑定为单一 Qt 生命周期单元。
 * @author hxxcxx
 * @date 2026-07-19
 */
#pragma once

#include <QWidget>

#include <memory>

namespace mulan::editor {
class DocumentSession;
}
namespace mulan::view {
struct ViewConfig;
}

class DocumentViewport;

class DocumentPage final : public QWidget {
    Q_OBJECT
public:
    DocumentPage(std::unique_ptr<mulan::editor::DocumentSession> session, const mulan::view::ViewConfig& viewConfig,
                 QWidget* parent = nullptr);
    ~DocumentPage() override;

    bool init();
    void shutdown();

    DocumentViewport* viewport() const { return viewport_; }
    mulan::editor::DocumentSession* session() const { return session_.get(); }

private:
    std::unique_ptr<mulan::editor::DocumentSession> session_;
    DocumentViewport* viewport_ = nullptr;
    bool shutdown_ = false;
};
