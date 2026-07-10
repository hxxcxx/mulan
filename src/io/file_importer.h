/**
 * @file file_importer.h
 * @brief 文件导入器接口:解析文件产出中立 ParsedScene,不接触 Document。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * importer 只负责解析(parse),把文件内容转成中立的 ParsedScene。Document 装载
 * 由 ParsedSceneLoader 统一完成。这样"解析"与"装文档"解耦,层级/材质等信息
 * 在 ParsedScene 中完整保留。
 */
#pragma once

#include "import_result.h"
#include "io_export.h"
#include "parsed_scene.h"

#include <mulan/core/result/error.h>

#include <string>
#include <vector>

namespace mulan::io {

class IO_API IFileImporter {
public:
    virtual ~IFileImporter() = default;

    /// 解析文件为中立 ParsedScene。
    virtual core::Result<ParsedScene> parse(const std::string& path, const ImportOptions& options = {}) = 0;

    virtual std::vector<std::string> supportedExtensions() const = 0;
    virtual std::string name() const = 0;
};

}  // namespace mulan::io
