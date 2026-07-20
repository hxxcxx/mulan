/**
 * @file document_open_service.h
 * @brief 在独立协调线程执行完整文档导入，并将结果投递回 UI 线程。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <mulan/core/concurrency/thread_pool.h>
#include <mulan/io/file_manager.h>

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class DocumentOpenService final : public QObject {
public:
    using RequestId = uint64_t;
    using Completion = std::function<void(RequestId, QString, mulan::Result<mulan::io::OpenDocumentResult>)>;

    DocumentOpenService();
    ~DocumentOpenService() override;

    DocumentOpenService(const DocumentOpenService&) = delete;
    DocumentOpenService& operator=(const DocumentOpenService&) = delete;

    [[nodiscard]] std::vector<std::string> supportedExtensions() const;

    /// 启动完整导入。完成回调始终排队回到本服务所属的 UI 线程。
    [[nodiscard]] RequestId openFile(QString filePath, Completion completion);

private:
    RequestId next_request_ = 1;
    // import_coordinator_ 必须先析构并排空任务，任务期间 file_manager_ 始终有效。
    mulan::io::FileManager file_manager_;
    mulan::core::ThreadPool import_coordinator_;
};
