/**
 * @file occt_importer.h
 * @brief OCCT 文件导入器，解析 STEP/IGES 后向 Document 添加形体。
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "file_importer.h"

namespace mulan::io {
class Document;
}

namespace mulan::io {

class IO_API OCCTImporter : public IFileImporter {
public:
    core::Result<ImportResult> import(const std::string& path, mulan::io::Document& doc,
                                      const ImportOptions& options = {}) override;

    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

}  // namespace mulan::io
