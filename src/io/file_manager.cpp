#include "file_manager.h"
#include "file_importer.h"
#include "importer_factory.h"

#include <mulan/document/document.h>

#include <algorithm>
#include <filesystem>

namespace mulan::io {

std::unique_ptr<mulan::document::Document> FileManager::openFile(const std::string& path) {
    last_error_.clear();

    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto importer = ImporterFactory::instance().create(ext);
    if (!importer) {
        last_error_ = "No importer for extension: ." + ext;
        return nullptr;
    }

    std::string displayName = std::filesystem::path(path).filename().string();
    auto doc = std::make_unique<mulan::document::Document>(std::move(displayName));
    doc->setFilePath(path);

    if (!importer->import(path, *doc)) {
        last_error_ = importer->lastError();
        return nullptr;
    }

    return doc;
}

std::vector<std::string> FileManager::supportedExtensions() const {
    return ImporterFactory::instance().allSupportedExtensions();
}

} // namespace mulan::io
