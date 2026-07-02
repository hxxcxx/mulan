#pragma once

#include <mulan/asset/asset_id.h>

#include <vector>

namespace mulan::scene {

struct RenderComponent {
    bool visible = true;
    std::vector<asset::AssetId> material_slots;
};

} // namespace mulan::scene

