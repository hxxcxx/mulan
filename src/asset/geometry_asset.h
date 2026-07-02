/**
 * @file geometry_asset.h
 * @brief GeometryAsset —— 场景实体引用的几何资产基类
 * @author hxxcxx
 * @date 2026-07-02
 */

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
