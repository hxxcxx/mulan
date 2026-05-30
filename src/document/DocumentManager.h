/**
 * @file DocumentManager.h
 * @brief 文档管理器 — 管理所有打开的 Document
 * @author hxxcxx
 * @date 2026-04-22
 *
 * DocumentManager 是创建 Document 的唯一入口。
 * openFile() → 选Importer → 解析 → 创建 Document → 返回指针。
 * 外部通过 Document* 引用，关闭后指针失效。
 */
#pragma once

#include "DocumentExport.h"

#include <memory>
#include <string>
#include <vector>

namespace mulan::document {

class Document;

class DOCUMENT_API DocumentManager {
public:
    DocumentManager() = default;
    ~DocumentManager();

    DocumentManager(const DocumentManager&) = delete;
    DocumentManager& operator=(const DocumentManager&) = delete;

    /// 打开文件 → 自动匹配 Importer → 创建 Document
    /// @return 成功返回 Document 指针，失败返回 nullptr
    Document* openFile(const std::string& path);

    /// 关闭文档
    void closeDocument(Document* doc);

    /// 获取最近一次 openFile 的错误信息
    const std::string& lastError() const { return m_lastError; }

    /// 当前活跃文档
    Document* activeDocument() const { return m_active; }
    void setActive(Document* doc) { m_active = doc; }

    /// 所有已打开的文档
    const std::vector<std::unique_ptr<Document>>& documents() const { return m_documents; }

    /// 所有支持的文件扩展名
    std::vector<std::string> supportedExtensions() const;

private:
    std::vector<std::unique_ptr<Document>> m_documents;
    Document* m_active = nullptr;
    std::string m_lastError;
};

} // namespace mulan::document
