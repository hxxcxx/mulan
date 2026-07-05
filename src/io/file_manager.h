/**
 * @file file_manager.h
 * @brief 文件管理器，按文件类型选择 importer 并返回 Document。
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "import_result.h"
#include "io_export.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::io {

struct OpenDocumentResult {
    std::unique_ptr<mulan::io::Document> document;
    ImportResult import;
};

class IO_API FileManager {
public:
    FileManager() = default;
    ~FileManager() = default;

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    core::Result<OpenDocumentResult> openFile(const std::string& path, const ImportOptions& options = {});

    std::vector<std::string> supportedExtensions() const;
};

}  // namespace mulan::io
