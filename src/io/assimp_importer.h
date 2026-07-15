/**
 * @file assimp_importer.h
 * @brief Assimp 文件导入器:解析常见模型格式为中立 ParsedScene。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include "file_importer.h"

namespace mulan::io {

class IO_API AssimpImporter : public IFileImporter {
public:
    Result<ParsedScene> parse(const std::string& path, const ImportOptions& options = {}) override;

    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

}  // namespace mulan::io
