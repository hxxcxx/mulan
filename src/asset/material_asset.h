/**
 * @file material_asset.h
 * @brief MaterialAsset —— 可编辑材质资产骨架
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "asset.h"

#include <mulan/engine/math/math.h>

#include <utility>

namespace mulan::asset {

class MaterialAsset : public Asset {
public:
    MaterialAsset(AssetId id, std::string name)
        : Asset(id, AssetKind::Material, std::move(name)) {}

    const engine::Vec3& baseColor() const { return base_color_; }
    void setBaseColor(const engine::Vec3& color) { base_color_ = color; }

    double roughness() const { return roughness_; }
    void setRoughness(double roughness) { roughness_ = roughness; }

    double metallic() const { return metallic_; }
    void setMetallic(double metallic) { metallic_ = metallic; }

private:
    engine::Vec3 base_color_{0.8, 0.8, 0.8};
    double roughness_ = 0.5;
    double metallic_ = 0.0;
};

} // namespace mulan::asset
