/**
 * @file assimp_importer.h
 * @brief Assimp 文件导入器，将常见模型格式转换为标准网格资产。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include "file_importer.h"

namespace mulan::io {
class Document;
}

namespace mulan::io {

class IO_API AssimpImporter : public IFileImporter {
public:
    core::Result<ImportResult> import(const std::string& path, mulan::io::Document& doc,
                                      const ImportOptions& options = {}) override;

    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

}  // namespace mulan::io
