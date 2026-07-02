/**
 * @file geometry_component.h
 * @brief GeometryComponent —— 场景实体引用的几何资产
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <mulan/asset/asset_id.h>

namespace mulan::scene {

struct GeometryComponent {
    asset::AssetId geometry = asset::AssetId::invalid();
};

} // namespace mulan::scene
