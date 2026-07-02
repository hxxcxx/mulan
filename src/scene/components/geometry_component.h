#pragma once

#include <mulan/asset/asset_id.h>

namespace mulan::scene {

struct GeometryComponent {
    asset::AssetId geometry = asset::AssetId::invalid();
};

} // namespace mulan::scene

