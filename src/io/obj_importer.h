/**
 * @file obj_importer.h
 * @brief Wavefront OBJ/MTL 文件导入器。
 * @author hxxcxx
 * @date 2026-07-19
 */
#pragma once

#include "file_importer.h"

namespace mulan::io {

/// 使用 RapidObj 解析 OBJ/MTL，并转换为项目统一的 ParsedScene。
class IO_API ObjImporter final : public IFileImporter {
public:
    Result<ParsedScene> parse(const std::string& path, const ImportOptions& options = {}) override;

    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

}  // namespace mulan::io
