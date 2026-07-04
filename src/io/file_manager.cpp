#include "file_manager.h"
#include "file_importer.h"
#include "importer_factory.h"

#include <mulan/core/result/error.h>
#include <mulan/io/document.h>

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>

namespace mulan::io {

std::expected<OpenDocumentResult, core::Error>
FileManager::openFile(const std::string& path, const ImportOptions& options) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto importer = ImporterFactory::instance().create(ext);
    if (!importer) {
        return std::unexpected(core::Error::make(
            core::ErrorCode::NotSupported,
            "No importer for extension: ." + ext));
    }

    std::string displayName = std::filesystem::path(path).filename().string();
    auto doc = std::make_unique<mulan::io::Document>(std::move(displayName));
    doc->setFilePath(path);

    auto importResult = importer->import(path, *doc, options);
    if (!importResult) {
        return std::unexpected(importResult.error());
    }

    return OpenDocumentResult{std::move(doc), std::move(*importResult)};
}

std::vector<std::string> FileManager::supportedExtensions() const {
    return ImporterFactory::instance().allSupportedExtensions();
}

} // namespace mulan::io
