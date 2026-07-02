#include "asset.h"

#include <utility>

namespace mulan::asset {

Asset::Asset(AssetId id, AssetKind kind, std::string name)
    : id_(id)
    , kind_(kind)
    , name_(std::move(name)) {}

} // namespace mulan::asset
