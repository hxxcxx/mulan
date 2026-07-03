#include "material_instance.h"
#include "material_cache.h"

namespace mulan::engine {

// ============================================================
// MaterialInstance
// ============================================================

MaterialInstance::MaterialInstance(const MaterialAsset* asset)
    : template_id_(asset ? asset->id() : 0) {
    if (asset) {
        material_ = asset->get();
    } else {
        material_ = Material::defaultPBR();
    }
}

MaterialInstance::MaterialInstance(Material material)
    : material_(std::move(material))
    , template_id_(0) {
}

// --- 基础参数修改 ---

void MaterialInstance::setBaseColor(const math::Vec3& color) {
    material_.baseColor = color;
    overrides_.baseColor = color;
    setFlag(override_flags_, MaterialOverrideFlags::BaseColor);
    dirty_ = true;
}

void MaterialInstance::setMetallic(double metallic) {
    material_.metallic = metallic;
    overrides_.metallic = metallic;
    setFlag(override_flags_, MaterialOverrideFlags::Metallic);
    dirty_ = true;
}

void MaterialInstance::setRoughness(double roughness) {
    material_.roughness = roughness;
    overrides_.roughness = roughness;
    setFlag(override_flags_, MaterialOverrideFlags::Roughness);
    dirty_ = true;
}

void MaterialInstance::setAlpha(double alpha) {
    material_.alpha = alpha;
    overrides_.alpha = alpha;
    setFlag(override_flags_, MaterialOverrideFlags::Alpha);
    dirty_ = true;
}

void MaterialInstance::setEmissive(const math::Vec3& color, double strength) {
    material_.emissive = color;
    material_.emissiveStrength = strength;
    overrides_.emissive = color;
    overrides_.emissiveStrength = strength;
    setFlag(override_flags_, MaterialOverrideFlags::Emissive);
    setFlag(override_flags_, MaterialOverrideFlags::EmissiveStrength);
    dirty_ = true;
}

void MaterialInstance::setDoubleSided(bool doubleSided) {
    material_.doubleSided = doubleSided;
    overrides_.doubleSided = doubleSided;
    setFlag(override_flags_, MaterialOverrideFlags::DoubleSided);
    dirty_ = true;
}

void MaterialInstance::setAlphaCutoff(double cutoff) {
    material_.alphaCutoff = cutoff;
    overrides_.alphaCutoff = cutoff;
    setFlag(override_flags_, MaterialOverrideFlags::AlphaCutoff);
    dirty_ = true;
}

void MaterialInstance::setSpecular(const math::Vec3& color) {
    material_.specular = color;
    overrides_.specular = color;
    setFlag(override_flags_, MaterialOverrideFlags::Specular);
    dirty_ = true;
}

void MaterialInstance::setShininess(double shininess) {
    material_.shininess = shininess;
    overrides_.shininess = shininess;
    setFlag(override_flags_, MaterialOverrideFlags::Shininess);
    dirty_ = true;
}

void MaterialInstance::setAO(double ao) {
    material_.ao = ao;
    overrides_.ao = ao;
    setFlag(override_flags_, MaterialOverrideFlags::AO);
    dirty_ = true;
}

void MaterialInstance::setMaterialType(MaterialType type) {
    material_.type = type;
    overrides_.type = type;
    setFlag(override_flags_, MaterialOverrideFlags::MaterialType);
    dirty_ = true;
}

void MaterialInstance::setAlphaMode(AlphaMode mode) {
    material_.alphaMode = mode;
    overrides_.alphaMode = mode;
    setFlag(override_flags_, MaterialOverrideFlags::AlphaMode);
    dirty_ = true;
}

// --- 纹理设置 ---

void MaterialInstance::setAlbedoTexture(uint16_t textureId) {
    material_.setAlbedoTexture(textureId);
    overrides_.setAlbedoTexture(textureId);
    setFlag(override_flags_, MaterialOverrideFlags::Textures);
    dirty_ = true;
}

void MaterialInstance::setNormalTexture(uint16_t textureId) {
    material_.setNormalTexture(textureId);
    overrides_.setNormalTexture(textureId);
    setFlag(override_flags_, MaterialOverrideFlags::Textures);
    dirty_ = true;
}

void MaterialInstance::setMetallicRoughnessTexture(uint16_t textureId) {
    material_.setMetallicRoughnessTexture(textureId);
    overrides_.setMetallicRoughnessTexture(textureId);
    setFlag(override_flags_, MaterialOverrideFlags::Textures);
    dirty_ = true;
}

void MaterialInstance::setEmissiveTexture(uint16_t textureId) {
    material_.setEmissiveTexture(textureId);
    overrides_.setEmissiveTexture(textureId);
    setFlag(override_flags_, MaterialOverrideFlags::Textures);
    dirty_ = true;
}

void MaterialInstance::setAoTexture(uint16_t textureId) {
    material_.setAoTexture(textureId);
    overrides_.setAoTexture(textureId);
    setFlag(override_flags_, MaterialOverrideFlags::Textures);
    dirty_ = true;
}

void MaterialInstance::setTextureBySlot(TextureSlot slot, uint16_t textureId) {
    material_.setTextureBySlot(slot, textureId);
    overrides_.setTextureBySlot(slot, textureId);
    setFlag(override_flags_, MaterialOverrideFlags::Textures);
    dirty_ = true;
}

void MaterialInstance::setTexturePath(TextureSlot slot, const std::string& path) {
    size_t idx = static_cast<size_t>(slot);
    material_.texturePaths[idx] = path;
    overrides_.texturePaths[idx] = path;
    dirty_ = true;
}

// --- 便捷方法 ---

void MaterialInstance::setHighlight(const math::Vec3& highlightColor) {
    if (!highlighted_) {
        saved_material_ = material_;
        highlighted_ = true;
    }
    material_.baseColor = highlightColor;
    material_.emissive = highlightColor * 0.3;
    material_.emissiveStrength = 1.0;
    dirty_ = true;
}

void MaterialInstance::clearHighlight() {
    if (highlighted_) {
        material_ = saved_material_;
        highlighted_ = false;
        dirty_ = true;
    }
}

void MaterialInstance::resetToTemplate() {
    if (template_id_ != 0) {
        auto* asset = MaterialCache::instance().findById(template_id_);
        if (asset) {
            material_ = asset->get();
            override_flags_ = MaterialOverrideFlags::None;
            overrides_ = Material{};
            dirty_ = true;
            highlighted_ = false;
        }
    }
}

void MaterialInstance::resetOverrides() {
    override_flags_ = MaterialOverrideFlags::None;
    overrides_ = Material{};
    rebuildFromTemplate();
    dirty_ = true;
}

void MaterialInstance::rebuildFromTemplate() {
    if (template_id_ == 0) return;

    auto* asset = MaterialCache::instance().findById(template_id_);
    if (!asset) return;

    Material base = asset->get();

    // 应用覆盖
    if (hasFlag(override_flags_, MaterialOverrideFlags::BaseColor))
        base.baseColor = overrides_.baseColor;
    if (hasFlag(override_flags_, MaterialOverrideFlags::Alpha))
        base.alpha = overrides_.alpha;
    if (hasFlag(override_flags_, MaterialOverrideFlags::Metallic))
        base.metallic = overrides_.metallic;
    if (hasFlag(override_flags_, MaterialOverrideFlags::Roughness))
        base.roughness = overrides_.roughness;
    if (hasFlag(override_flags_, MaterialOverrideFlags::AO))
        base.ao = overrides_.ao;
    if (hasFlag(override_flags_, MaterialOverrideFlags::Specular))
        base.specular = overrides_.specular;
    if (hasFlag(override_flags_, MaterialOverrideFlags::Shininess))
        base.shininess = overrides_.shininess;
    if (hasFlag(override_flags_, MaterialOverrideFlags::Emissive))
        base.emissive = overrides_.emissive;
    if (hasFlag(override_flags_, MaterialOverrideFlags::EmissiveStrength))
        base.emissiveStrength = overrides_.emissiveStrength;
    if (hasFlag(override_flags_, MaterialOverrideFlags::AlphaCutoff))
        base.alphaCutoff = overrides_.alphaCutoff;
    if (hasFlag(override_flags_, MaterialOverrideFlags::DoubleSided))
        base.doubleSided = overrides_.doubleSided;
    if (hasFlag(override_flags_, MaterialOverrideFlags::MaterialType))
        base.type = overrides_.type;
    if (hasFlag(override_flags_, MaterialOverrideFlags::AlphaMode))
        base.alphaMode = overrides_.alphaMode;
    if (hasFlag(override_flags_, MaterialOverrideFlags::Textures))
        base.textures = overrides_.textures;

    material_ = std::move(base);
    highlighted_ = false;
}

} // namespace mulan::engine
