#pragma once

#include "asset.h"

#include <utility>

namespace mulan::asset {

class GeometryAsset : public Asset {
public:
    GeometryAsset(AssetId id, AssetKind kind, std::string name)
        : Asset(id, kind, std::move(name)) {}
};

} // namespace mulan::asset
