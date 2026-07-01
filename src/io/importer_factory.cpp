#include "importer_factory.h"
#include "file_importer.h"

#include <cctype>

namespace mulan::io {

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
    creators_[toLower(extension)] = std::move(creator);
}

std::unique_ptr<IFileImporter> ImporterFactory::create(const std::string& extension) const {
    auto it = creators_.find(toLower(extension));
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> ImporterFactory::allSupportedExtensions() const {
    std::vector<std::string> exts;
    exts.reserve(creators_.size());
    for (const auto& [ext, _] : creators_) {
        exts.push_back(ext);
    }
    return exts;
}

AutoRegisterImporter::AutoRegisterImporter(const std::string& extension, ImporterFactory::Creator creator) {
    ImporterFactory::instance().registerImporter(extension, std::move(creator));
}

} // namespace mulan::io
