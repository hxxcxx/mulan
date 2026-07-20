/**
 * @file document_page.h
 * @brief 将一个 DocumentSession 与其 DocumentViewport 绑定为单一 Qt 生命周期单元。
 * @author hxxcxx
 * @date 2026-07-19
 */
#pragma once

#include <QWidget>

#include <cstdint>
#include <memory>

namespace mulan::editor {
class DocumentSession;
}
namespace mulan::view {
struct ViewConfig;
}

class DocumentViewport;
class QWidget;

class DocumentPage final : public QWidget {
    Q_OBJECT
public:
    DocumentPage(std::unique_ptr<mulan::editor::DocumentSession> session, const mulan::view::ViewConfig& viewConfig,
                 uint64_t openRequestId = 0, QWidget* parent = nullptr);
    ~DocumentPage() override;

    bool init();
    void shutdown();
    bool completeOpen(uint64_t requestId, std::unique_ptr<mulan::editor::DocumentSession> session);

    DocumentViewport* viewport() const { return viewport_; }
    mulan::editor::DocumentSession* session() const { return session_.get(); }
    uint64_t openRequestId() const { return open_request_id_; }

private:
    std::unique_ptr<mulan::editor::DocumentSession> session_;
    DocumentViewport* viewport_ = nullptr;
    QWidget* loading_overlay_ = nullptr;
    uint64_t open_request_id_ = 0;
    bool shutdown_ = false;
};
