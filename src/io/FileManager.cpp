/**
 * @file FileManager.cpp
 * @brief FileManager 实现 — 打开文件直接填充 World
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "FileManager.h"
#include "IFileImporter.h"
#include "ImporterFactory.h"

#include <mulan/world/World.h>

#include <algorithm>
#include <filesystem>

namespace mulan::io {

std::unique_ptr<mulan::world::World> FileManager::openFile(const std::string& path) {
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

    auto world = std::make_unique<mulan::world::World>();
    if (!importer->import(path, *world)) {
        m_lastError = importer->lastError();
        return nullptr;
    }

    return world;
}

std::vector<std::string> FileManager::supportedExtensions() const {
    return ImporterFactory::instance().allSupportedExtensions();
}

} // namespace mulan::io
