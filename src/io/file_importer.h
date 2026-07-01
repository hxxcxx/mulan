/**
 * @file file_importer.h
 * @brief 文件导入器接口 — 解析文件并填充 Document
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "io_export.h"

#include <memory>
#include <string>
#include <vector>

namespace mulan::document {
class Document;
}

namespace mulan::io {

/// 文件导入器接口 — 解析文件，向 Document 添加数据（B-Rep + Entity）
class IO_API IFileImporter {
public:
    virtual ~IFileImporter() = default;

    /// 导入文件，向 Document 添加数据
    /// @return true 成功，false 失败（调用 lastError() 获取原因）
    virtual bool import(const std::string& path, mulan::document::Document& doc) = 0;

    /// 支持的文件扩展名（小写，不含点）
    virtual std::vector<std::string> supportedExtensions() const = 0;

    /// 导入器名称
    virtual std::string name() const = 0;

    const std::string& lastError() const { return last_error_; }

protected:
    std::string last_error_;
};

} // namespace mulan::io
