#include "document_open_service.h"

#include <mulan/core/log/log.h>
#include <mulan/core/result/error.h>

#include <QMetaObject>

#include <exception>
#include <memory>
#include <string>
#include <utility>

DocumentOpenService::DocumentOpenService() : import_coordinator_(1) {
}

DocumentOpenService::~DocumentOpenService() = default;

std::vector<std::string> DocumentOpenService::supportedExtensions() const {
    return file_manager_.supportedExtensions();
}

DocumentOpenService::RequestId DocumentOpenService::openFile(QString filePath, Completion completion) {
    if (filePath.isEmpty() || !completion) {
        return 0;
    }

    RequestId requestId = next_request_++;
    if (requestId == 0) {
        requestId = next_request_++;
    }

    const std::string sourcePath = filePath.toStdString();
    (void) import_coordinator_.submit([this, requestId, filePath = std::move(filePath), sourcePath,
                                       completion = std::move(completion)]() mutable {
        auto result = [&]() -> mulan::Result<mulan::io::OpenDocumentResult> {
            try {
                return file_manager_.openFile(sourcePath);
            } catch (const std::exception& error) {
                return std::unexpected(
                        mulan::Error::make(mulan::ErrorCode::Internal,
                                           std::string("Unhandled exception while opening document: ") + error.what()));
            } catch (...) {
                return std::unexpected(
                        mulan::Error::make(mulan::ErrorCode::Internal, "Unhandled exception while opening document."));
            }
        }();

        // Qt 的 queued functor 以可复制对象传递；共享结果仅用于跨线程所有权移交。
        auto sharedResult = std::make_shared<mulan::Result<mulan::io::OpenDocumentResult>>(std::move(result));
        const bool queued = QMetaObject::invokeMethod(
                this,
                [requestId, filePath = std::move(filePath), completion = std::move(completion),
                 sharedResult = std::move(sharedResult)]() mutable {
                    completion(requestId, std::move(filePath), std::move(*sharedResult));
                },
                Qt::QueuedConnection);
        if (!queued) {
            LOG_WARN("[App] Document import completion could not be queued: request={}", requestId);
        }
    });
    return requestId;
}
