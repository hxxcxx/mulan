#include "asset.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace mulan::asset {

Asset::Asset(AssetId id, AssetKind kind, std::string name) : id_(id), kind_(kind), name_(std::move(name)) {
}

void Asset::setName(std::string name) {
    assignIfChanged(name_, std::move(name));
}

void Asset::touch() {
    if (revision_ == std::numeric_limits<AssetRevision>::max())
        throw std::overflow_error("Asset revision exhausted");
    ++revision_;
    if (change_callback_) {
        change_callback_(id_);
    }
}

}  // namespace mulan::asset
