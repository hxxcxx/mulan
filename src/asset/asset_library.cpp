#include "asset_library.h"

namespace mulan::asset {

AssetId AssetLibrary::allocateId() {
    return AssetId{next_id_++};
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

void AssetLibrary::remove(AssetId id) {
    assets_.erase(id);
}

void AssetLibrary::clear() {
    assets_.clear();
}

size_t AssetLibrary::count(AssetKind kind) const {
    size_t result = 0;
    for (const auto& [id, asset] : assets_) {
        if (asset && asset->kind() == kind)
            ++result;
    }
    return result;
}

} // namespace mulan::asset
