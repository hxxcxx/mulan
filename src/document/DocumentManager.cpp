/**
 * @file DocumentManager.cpp
 * @brief DocumentManager 实现
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "DocumentManager.h"
#include "Document.h"
#include "IFileImporter.h"
#include "ImporterFactory.h"

#include <algorithm>
#include <filesystem>

namespace mulan::document {

DocumentManager::~DocumentManager() = default;

Document* DocumentManager::openFile(const std::string& path) {
    m_lastError.clear();

    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto importer = ImporterFactory::instance().create(ext);
    if (!importer) {
        m_lastError = "No importer for extension: ." + ext;
        return nullptr;
    }

    auto result = importer->importFile(path);
    if (!result.success) {
        m_lastError = result.error;
        return nullptr;
    }

    auto* ptr = result.document.get();
    m_documents.push_back(std::move(result.document));
    m_active = ptr;
    return ptr;
}

void DocumentManager::closeDocument(Document* doc) {
    if (!doc) return;
    if (m_active == doc) m_active = nullptr;
    std::erase_if(m_documents, [doc](auto& p) { return p.get() == doc; });
    if (!m_active && !m_documents.empty()) {
        m_active = m_documents.back().get();
    }
}

std::vector<std::string> DocumentManager::supportedExtensions() const {
    return ImporterFactory::instance().allSupportedExtensions();
}

} // namespace mulan::document
