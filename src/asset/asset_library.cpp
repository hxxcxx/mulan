#include "asset_library.h"

#include <limits>
#include <stdexcept>

namespace mulan::asset {

AssetId AssetLibrary::allocateId() {
    return AssetId{ next_id_++ };
}

Asset* AssetLibrary::asset(AssetId id) {
    auto it = assets_.find(id);
    return it != assets_.end() ? it->second.get() : nullptr;
}

const Asset* AssetLibrary::asset(AssetId id) const {
    auto it = assets_.find(id);
    return it != assets_.end() ? it->second.get() : nullptr;
}

bool AssetLibrary::contains(AssetId id) const {
    return assets_.find(id) != assets_.end();
}

bool AssetLibrary::remove(AssetId id) {
    if (assets_.erase(id) == 0)
        return false;
    touch();
    return true;
}

void AssetLibrary::clear() {
    if (assets_.empty())
        return;
    assets_.clear();
    touch();
}

std::optional<AssetRevision> AssetLibrary::contentRevision(AssetId id) const {
    const Asset* value = asset(id);
    if (!value)
        return std::nullopt;
    return value->revision();
}

void AssetLibrary::touch() {
    if (membership_revision_ == std::numeric_limits<AssetLibraryRevision>::max())
        throw std::overflow_error("AssetLibrary revision exhausted");
    ++membership_revision_;
}

size_t AssetLibrary::count(AssetKind kind) const {
    size_t result = 0;
    for (const auto& [id, asset] : assets_) {
        if (asset && asset->kind() == kind)
            ++result;
    }
    return result;
}

}  // namespace mulan::asset
