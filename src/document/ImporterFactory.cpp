/**
 * @file ImporterFactory.cpp
 * @brief ImporterFactory 实现
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "ImporterFactory.h"
#include "IFileImporter.h"
#include "Document.h"

#include <cctype>

namespace mulan::document {

namespace {

std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // anonymous namespace

ImporterFactory& ImporterFactory::instance() {
    static ImporterFactory factory;
    return factory;
}

void ImporterFactory::registerImporter(const std::string& extension, Creator creator) {
    m_creators[toLower(extension)] = std::move(creator);
}

std::unique_ptr<IFileImporter> ImporterFactory::create(const std::string& extension) const {
    auto it = m_creators.find(toLower(extension));
    if (it != m_creators.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> ImporterFactory::allSupportedExtensions() const {
    std::vector<std::string> exts;
    exts.reserve(m_creators.size());
    for (const auto& [ext, _] : m_creators) {
        exts.push_back(ext);
    }
    return exts;
}

AutoRegisterImporter::AutoRegisterImporter(const std::string& extension, ImporterFactory::Creator creator) {
    ImporterFactory::instance().registerImporter(extension, std::move(creator));
}

} // namespace mulan::Document
