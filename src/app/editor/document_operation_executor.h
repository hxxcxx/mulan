/**
 * @file document_operation_executor.h
 * @brief DocumentOperationExecutor 将编辑器文档操作应用到当前文档会话。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_operation.h"

class DocumentSession;
class DocumentViewBinding;

namespace mulan::app {

class DocumentOperationExecutor {
public:
    void bind(DocumentSession* session, DocumentViewBinding* binding);
    void unbind();

    bool isBound() const { return session_ != nullptr; }
    bool execute(DocumentOperation operation) const;

private:
    DocumentSession* session_ = nullptr;
    DocumentViewBinding* binding_ = nullptr;
};

}  // namespace mulan::app
