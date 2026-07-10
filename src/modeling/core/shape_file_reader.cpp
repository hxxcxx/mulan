#include "shape_file_reader.h"

#include <algorithm>
#include <cctype>

namespace mulan::modeling {

namespace {
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

ShapeFileReaderRegistry& ShapeFileReaderRegistry::instance() {
    static ShapeFileReaderRegistry registry;
    return registry;
}

void ShapeFileReaderRegistry::registerReader(Creator creator) {
    auto probe = creator();
    if (!probe)
        return;

    std::vector<std::string> exts;
    for (auto& e : probe->supportedExtensions())
        exts.push_back(toLower(e));

    Entry entry;
    entry.creator = std::move(creator);
    entry.extensions = exts;

    const size_t index = entries_.size();
    entries_.push_back(std::move(entry));

    for (const auto& e : exts)
        byExtension_[e] = index;
}

std::unique_ptr<IShapeFileReader> ShapeFileReaderRegistry::create(const std::string& extension) const {
    const auto key = toLower(extension);
    auto it = byExtension_.find(key);
    if (it == byExtension_.end())
        return nullptr;
    return entries_[it->second].creator();
}

std::vector<std::string> ShapeFileReaderRegistry::allSupportedExtensions() const {
    std::vector<std::string> all;
    for (const auto& entry : entries_)
        all.insert(all.end(), entry.extensions.begin(), entry.extensions.end());
    return all;
}

}  // namespace mulan::modeling
