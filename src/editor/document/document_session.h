/**
 * @file document_session.h
 * @brief 表示应用中一个已打开文档的会话状态。
 *
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include <mulan/io/document.h>
#include <mulan/io/import_result.h>

#include <cstdint>
#include <memory>
#include <string>

namespace mulan::editor {
class CommandHistory;
class DocumentOperationExecutor;
}  // namespace mulan::editor

enum class DocumentSessionKind : uint8_t {
    Draft,
    Imported,
};

struct DocumentRenderPreferences {
    bool preferOrthographic = true;
    bool preferIBL = true;
    bool preferPBRSurface = false;
};

class DocumentSession {
public:
    explicit DocumentSession(std::unique_ptr<mulan::io::Document> doc);
    DocumentSession(std::unique_ptr<mulan::io::Document> doc, mulan::io::ImportReport report);
    ~DocumentSession();

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    mulan::io::Document* document() { return document_.get(); }
    const mulan::io::Document* document() const { return document_.get(); }

    const std::string& displayName() const;

    const DocumentRenderPreferences& renderPreferences() const { return preferences_; }
    bool preferOrthographic() const { return preferences_.preferOrthographic; }
    bool preferIBL() const { return preferences_.preferIBL; }
    bool preferPBRSurface() const { return preferences_.preferPBRSurface; }
    DocumentSessionKind kind() const { return kind_; }
    bool allowsDrawingCommands() const { return kind_ == DocumentSessionKind::Draft; }
    /// 草稿文档当前没有保存工作流，关闭时不应伪装成可保存文档打断用户。
    bool requiresDiscardConfirmation() const {
        return kind_ != DocumentSessionKind::Draft && document_ && document_->isDirty();
    }

private:
    friend class mulan::editor::DocumentOperationExecutor;

    /// 历史只向文档操作执行器开放，不对会话外部暴露可变容器。
    mulan::editor::CommandHistory& commandHistory() noexcept;

    std::unique_ptr<mulan::io::Document> document_;
    /// 命令历史属于文档会话，视图/executor 重绑定不会改变其生命周期。
    std::unique_ptr<mulan::editor::CommandHistory> command_history_;
    DocumentRenderPreferences preferences_;
    DocumentSessionKind kind_ = DocumentSessionKind::Draft;
};
