/**
 * @file gltf_importer.h
 * @brief glTF 2.0 导入器(基于 fastgltf):解析为中立 ParsedScene。
 * @author hxxcxx
 * @date 2026-07-04
 */
#pragma once

#include "file_importer.h"

namespace mulan::io {

class IO_API GltfImporter : public IFileImporter {
public:
    Result<ParsedScene> parse(const std::string& path, const ImportOptions& options = {}) override;

    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

}  // namespace mulan::io
