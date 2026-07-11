/**
 * @file material_asset.h
 * @brief MaterialAsset 保存文档层可编辑材质语义。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "asset.h"

#include <mulan/graphics/material_types.h>
#include <mulan/math/math.h>

#include <cstdint>
#include <utility>

namespace mulan::asset {

using AlphaMode = mulan::graphics::AlphaMode;

class MaterialAsset : public Asset {
public:
    MaterialAsset(AssetId id, std::string name) : Asset(id, AssetKind::Material, std::move(name)) {}

    const math::Vec4& baseColorFactor() const { return base_color_factor_; }
    void setBaseColorFactor(const math::Vec4& color) { base_color_factor_ = color; }

    math::Vec3 baseColor() const { return { base_color_factor_.x, base_color_factor_.y, base_color_factor_.z }; }
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

    AssetId emissiveTexture() const { return emissive_texture_; }
    void setEmissiveTexture(AssetId texture) { emissive_texture_ = texture; }

    const math::Vec3& emissiveFactor() const { return emissive_factor_; }
    void setEmissiveFactor(const math::Vec3& factor) { emissive_factor_ = factor; }

    double emissiveStrength() const { return emissive_strength_; }
    void setEmissiveStrength(double strength) { emissive_strength_ = strength; }

    AssetId occlusionTexture() const { return occlusion_texture_; }
    void setOcclusionTexture(AssetId texture) { occlusion_texture_ = texture; }

    /// 每 slot 的 sRGB 意图（sRGB 下放到使用方而非 TextureAsset）。
    /// albedo / emissive 等颜色贴图 → true；normal / mr / ao 等数据贴图 → false。
    bool baseColorTextureSrgb() const { return base_color_texture_srgb_; }
    void setBaseColorTextureSrgb(bool v) { base_color_texture_srgb_ = v; }

    bool normalTextureSrgb() const { return normal_texture_srgb_; }
    void setNormalTextureSrgb(bool v) { normal_texture_srgb_ = v; }

    bool metallicRoughnessTextureSrgb() const { return metallic_roughness_texture_srgb_; }
    void setMetallicRoughnessTextureSrgb(bool v) { metallic_roughness_texture_srgb_ = v; }

    bool emissiveTextureSrgb() const { return emissive_texture_srgb_; }
    void setEmissiveTextureSrgb(bool v) { emissive_texture_srgb_ = v; }

    bool occlusionTextureSrgb() const { return occlusion_texture_srgb_; }
    void setOcclusionTextureSrgb(bool v) { occlusion_texture_srgb_ = v; }

    AlphaMode alphaMode() const { return alpha_mode_; }
    void setAlphaMode(AlphaMode mode) { alpha_mode_ = mode; }

    bool doubleSided() const { return double_sided_; }
    void setDoubleSided(bool value) { double_sided_ = value; }

private:
    math::Vec4 base_color_factor_{ 0.8, 0.8, 0.8, 1.0 };
    double roughness_ = 0.5;
    double metallic_ = 0.0;
    AssetId base_color_texture_ = AssetId::invalid();
    AssetId normal_texture_ = AssetId::invalid();
    AssetId metallic_roughness_texture_ = AssetId::invalid();
    AssetId emissive_texture_ = AssetId::invalid();
    math::Vec3 emissive_factor_{ 0.0, 0.0, 0.0 };
    double emissive_strength_ = 1.0;
    AssetId occlusion_texture_ = AssetId::invalid();
    bool base_color_texture_srgb_ = true;
    bool normal_texture_srgb_ = false;
    bool metallic_roughness_texture_srgb_ = false;
    bool emissive_texture_srgb_ = true;
    bool occlusion_texture_srgb_ = false;
    AlphaMode alpha_mode_ = AlphaMode::Opaque;
    bool double_sided_ = false;
};

}  // namespace mulan::asset
