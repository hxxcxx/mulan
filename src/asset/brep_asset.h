#pragma once

#include "geometry_asset.h"

#include <utility>

namespace mulan::asset {

class BRepAsset : public GeometryAsset {
public:
    explicit BRepAsset(AssetId id, std::string name)
        : GeometryAsset(id, AssetKind::BRep, std::move(name)) {}

    // Reserved for the future CAD/BIM document model.
    // The current phase intentionally avoids exposing OCCT from asset headers.
};

} // namespace mulan::asset
