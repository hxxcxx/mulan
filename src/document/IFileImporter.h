/**
 * @file IFileImporter.h
 * @brief 文件导入器接口 — 解析文件并填充 Document
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "DocumentExport.h"

#include <memory>
#include <string>
#include <vector>

namespace mulan::document {

class Document;

/// 导入结果
struct ImportResult {
    std::unique_ptr<Document> document;
    std::string error;
    bool success = false;
};

class DOCUMENT_API IFileImporter {
public:
    virtual ~IFileImporter() = default;

    /// 解析文件，返回包含 Entity 的 Document
    virtual ImportResult importFile(const std::string& path) = 0;

    /// 支持的文件扩展名（小写，不含点）
    virtual std::vector<std::string> supportedExtensions() const = 0;

    /// 导入器名称
    virtual std::string name() const = 0;
};

} // namespace mulan::document
