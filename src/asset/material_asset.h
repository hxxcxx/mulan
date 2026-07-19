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
using MaterialShadingModel = mulan::graphics::MaterialShadingModel;

class MaterialAsset : public Asset {
public:
    MaterialAsset(AssetId id, std::string name) : Asset(id, AssetKind::Material, std::move(name)) {}

    MaterialShadingModel shadingModel() const { return shading_model_; }
    void setShadingModel(MaterialShadingModel model) { assignIfChanged(shading_model_, model); }

    const math::Vec4& baseColorFactor() const { return base_color_factor_; }
    void setBaseColorFactor(const math::Vec4& color) { assignIfChanged(base_color_factor_, color); }

    math::Vec3 baseColor() const { return { base_color_factor_.x, base_color_factor_.y, base_color_factor_.z }; }
    void setBaseColor(const math::Vec3& color) {
        assignIfChanged(base_color_factor_, math::Vec4{ color.x, color.y, color.z, base_color_factor_.w });
    }

    const math::Vec3& ambientFactor() const { return ambient_factor_; }
    void setAmbientFactor(const math::Vec3& factor) { assignIfChanged(ambient_factor_, factor); }

    const math::Vec3& specularFactor() const { return specular_factor_; }
    void setSpecularFactor(const math::Vec3& factor) { assignIfChanged(specular_factor_, factor); }

    double shininess() const { return shininess_; }
    void setShininess(double value) { assignIfChanged(shininess_, value); }

    double roughness() const { return roughness_; }
    void setRoughness(double roughness) { assignIfChanged(roughness_, roughness); }

    double metallic() const { return metallic_; }
    void setMetallic(double metallic) { assignIfChanged(metallic_, metallic); }

    AssetId baseColorTexture() const { return base_color_texture_; }
    void setBaseColorTexture(AssetId texture) { assignIfChanged(base_color_texture_, texture); }

    AssetId normalTexture() const { return normal_texture_; }
    void setNormalTexture(AssetId texture) { assignIfChanged(normal_texture_, texture); }

    AssetId metallicRoughnessTexture() const { return metallic_roughness_texture_; }
    void setMetallicRoughnessTexture(AssetId texture) { assignIfChanged(metallic_roughness_texture_, texture); }

    AssetId emissiveTexture() const { return emissive_texture_; }
    void setEmissiveTexture(AssetId texture) { assignIfChanged(emissive_texture_, texture); }

    const math::Vec3& emissiveFactor() const { return emissive_factor_; }
    void setEmissiveFactor(const math::Vec3& factor) { assignIfChanged(emissive_factor_, factor); }

    double emissiveStrength() const { return emissive_strength_; }
    void setEmissiveStrength(double strength) { assignIfChanged(emissive_strength_, strength); }

    AssetId occlusionTexture() const { return occlusion_texture_; }
    void setOcclusionTexture(AssetId texture) { assignIfChanged(occlusion_texture_, texture); }

    AssetId ambientTexture() const { return ambient_texture_; }
    void setAmbientTexture(AssetId texture) { assignIfChanged(ambient_texture_, texture); }

    AssetId specularTexture() const { return specular_texture_; }
    void setSpecularTexture(AssetId texture) { assignIfChanged(specular_texture_, texture); }

    AssetId shininessTexture() const { return shininess_texture_; }
    void setShininessTexture(AssetId texture) { assignIfChanged(shininess_texture_, texture); }

    AssetId opacityTexture() const { return opacity_texture_; }
    void setOpacityTexture(AssetId texture) { assignIfChanged(opacity_texture_, texture); }

    /// 每 slot 的 sRGB 意图（sRGB 下放到使用方而非 TextureAsset）。
    /// albedo / emissive 等颜色贴图 → true；normal / mr / ao 等数据贴图 → false。
    bool baseColorTextureSrgb() const { return base_color_texture_srgb_; }
    void setBaseColorTextureSrgb(bool v) { assignIfChanged(base_color_texture_srgb_, v); }

    bool normalTextureSrgb() const { return normal_texture_srgb_; }
    void setNormalTextureSrgb(bool v) { assignIfChanged(normal_texture_srgb_, v); }

    bool metallicRoughnessTextureSrgb() const { return metallic_roughness_texture_srgb_; }
    void setMetallicRoughnessTextureSrgb(bool v) { assignIfChanged(metallic_roughness_texture_srgb_, v); }

    bool emissiveTextureSrgb() const { return emissive_texture_srgb_; }
    void setEmissiveTextureSrgb(bool v) { assignIfChanged(emissive_texture_srgb_, v); }

    bool occlusionTextureSrgb() const { return occlusion_texture_srgb_; }
    void setOcclusionTextureSrgb(bool v) { assignIfChanged(occlusion_texture_srgb_, v); }

    bool ambientTextureSrgb() const { return ambient_texture_srgb_; }
    void setAmbientTextureSrgb(bool v) { assignIfChanged(ambient_texture_srgb_, v); }

    bool specularTextureSrgb() const { return specular_texture_srgb_; }
    void setSpecularTextureSrgb(bool v) { assignIfChanged(specular_texture_srgb_, v); }

    bool shininessTextureSrgb() const { return false; }
    bool opacityTextureSrgb() const { return false; }

    AlphaMode alphaMode() const { return alpha_mode_; }
    void setAlphaMode(AlphaMode mode) { assignIfChanged(alpha_mode_, mode); }

    double alphaCutoff() const { return alpha_cutoff_; }
    void setAlphaCutoff(double value) { assignIfChanged(alpha_cutoff_, value); }

    bool doubleSided() const { return double_sided_; }
    void setDoubleSided(bool value) { assignIfChanged(double_sided_, value); }

private:
    MaterialShadingModel shading_model_ = MaterialShadingModel::MetallicRoughness;
    math::Vec4 base_color_factor_{ 0.8, 0.8, 0.8, 1.0 };
    math::Vec3 ambient_factor_{ 0.0, 0.0, 0.0 };
    math::Vec3 specular_factor_{ 0.5, 0.5, 0.5 };
    double shininess_ = 32.0;
    double roughness_ = 0.5;
    double metallic_ = 0.0;
    AssetId base_color_texture_ = AssetId::invalid();
    AssetId normal_texture_ = AssetId::invalid();
    AssetId metallic_roughness_texture_ = AssetId::invalid();
    AssetId emissive_texture_ = AssetId::invalid();
    math::Vec3 emissive_factor_{ 0.0, 0.0, 0.0 };
    double emissive_strength_ = 1.0;
    AssetId occlusion_texture_ = AssetId::invalid();
    AssetId ambient_texture_ = AssetId::invalid();
    AssetId specular_texture_ = AssetId::invalid();
    AssetId shininess_texture_ = AssetId::invalid();
    AssetId opacity_texture_ = AssetId::invalid();
    bool base_color_texture_srgb_ = true;
    bool normal_texture_srgb_ = false;
    bool metallic_roughness_texture_srgb_ = false;
    bool emissive_texture_srgb_ = true;
    bool occlusion_texture_srgb_ = false;
    bool ambient_texture_srgb_ = true;
    bool specular_texture_srgb_ = true;
    AlphaMode alpha_mode_ = AlphaMode::Opaque;
    double alpha_cutoff_ = 0.5;
    bool double_sided_ = false;
};

}  // namespace mulan::asset
