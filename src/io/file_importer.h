/**
 * @file file_importer.h
 * @brief 文件导入器接口，解析文件并填充 Document。
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "import_result.h"
#include "io_export.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <string>
#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::io {

class IO_API IFileImporter {
public:
    virtual ~IFileImporter() = default;

    virtual core::Result<ImportResult> import(const std::string& path,
                                              Document& doc,
                                              const ImportOptions& options = {}) = 0;

    virtual std::vector<std::string> supportedExtensions() const = 0;
    virtual std::string name() const = 0;
};

} // namespace mulan::io
