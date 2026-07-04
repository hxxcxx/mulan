/**
 * @file gltf_importer.h
 * @brief glTF 2.0 导入器（基于 fastgltf + nlohmann-json）
 * @author hxxcxx
 * @date 2026-07-04
 */
#pragma once

#include "file_importer.h"

namespace mulan::io {
class Document;
}

namespace mulan::io {

class IO_API GltfImporter : public IFileImporter {
public:
    std::expected<ImportResult, core::Error>
    import(const std::string& path,
           mulan::io::Document& doc,
           const ImportOptions& options = {}) override;

    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

} // namespace mulan::io
