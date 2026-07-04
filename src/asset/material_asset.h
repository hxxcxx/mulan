/**
 * @file material_asset.h
 * @brief MaterialAsset 保存文档层可编辑材质语义。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "asset.h"

#include <mulan/math/math.h>
#include <mulan/engine/render/material/material.h>  // engine::AlphaMode（统一枚举，分层清理待后续）

#include <cstdint>
#include <utility>

namespace mulan::asset {

using AlphaMode = mulan::engine::AlphaMode;  // 统一使用 engine::AlphaMode

class MaterialAsset : public Asset {
public:
    MaterialAsset(AssetId id, std::string name)
        : Asset(id, AssetKind::Material, std::move(name)) {}

    const math::Vec4& baseColorFactor() const { return base_color_factor_; }
    void setBaseColorFactor(const math::Vec4& color) { base_color_factor_ = color; }

    math::Vec3 baseColor() const {
        return {base_color_factor_.x, base_color_factor_.y, base_color_factor_.z};
    }
    void setBaseColor(const math::Vec3& color) {
        base_color_factor_.x = color.x;
        base_color_factor_.y = color.y;
        base_color_factor_.z = color.z;
    }

    double roughness() const { return roughness_; }
    void setRoughness(double roughness) { roughness_ = roughness; }

    double metallic() const { return metallic_; }
    void setMetallic(double metallic) { metallic_ = metallic; }

    AssetId baseColorTexture() const { return base_color_texture_; }
    void setBaseColorTexture(AssetId texture) { base_color_texture_ = texture; }

    AssetId normalTexture() const { return normal_texture_; }
    void setNormalTexture(AssetId texture) { normal_texture_ = texture; }

    AssetId metallicRoughnessTexture() const { return metallic_roughness_texture_; }
    void setMetallicRoughnessTexture(AssetId texture) { metallic_roughness_texture_ = texture; }

    AlphaMode alphaMode() const { return alpha_mode_; }
    void setAlphaMode(AlphaMode mode) { alpha_mode_ = mode; }

    bool doubleSided() const { return double_sided_; }
    void setDoubleSided(bool value) { double_sided_ = value; }

private:
    math::Vec4 base_color_factor_{0.8, 0.8, 0.8, 1.0};
    double roughness_ = 0.5;
    double metallic_ = 0.0;
    AssetId base_color_texture_ = AssetId::invalid();
    AssetId normal_texture_ = AssetId::invalid();
    AssetId metallic_roughness_texture_ = AssetId::invalid();
    AlphaMode alpha_mode_ = AlphaMode::Opaque;
    bool double_sided_ = false;
};

} // namespace mulan::asset
