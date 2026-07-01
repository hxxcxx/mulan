/**
 * @file file_manager.h
 * @brief 文件管理器 — 打开文件返回 Document
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 唯一入口：openFile() → 选 Importer → 创建 Document → 填充 → 返回所有权。
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

class IO_API FileManager {
public:
    FileManager() = default;
    ~FileManager() = default;

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    /// 打开文件 → 自动匹配 Importer → 填充 Document → 返回所有权
    std::unique_ptr<mulan::document::Document> openFile(const std::string& path);

    /// 获取最近一次 openFile 的错误信息
    const std::string& lastError() const { return last_error_; }

    /// 所有支持的文件扩展名
    std::vector<std::string> supportedExtensions() const;

private:
    std::string last_error_;
};

} // namespace mulan::io
