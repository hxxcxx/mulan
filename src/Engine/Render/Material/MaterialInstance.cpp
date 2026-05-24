/**
 * @file MaterialInstance.cpp
 * @brief 材质实例实现
 */

#include "MaterialInstance.h"
#include "MaterialCache.h"

namespace MulanGeo::engine {

// ============================================================
// MaterialInstance
// ============================================================

MaterialInstance::MaterialInstance(const MaterialAsset* asset)
    : m_templateId(asset ? asset->id() : 0) {
    if (asset) {
        m_material = asset->get();
    } else {
        m_material = Material::defaultPBR();
    }
}

MaterialInstance::MaterialInstance(Material material)
    : m_material(std::move(material))
    , m_templateId(0) {
}

// --- 基础参数修改 ---

void MaterialInstance::setBaseColor(const Vec3& color) {
    m_material.baseColor = color;
    m_overrides.baseColor = color;
    setFlag(m_overrideFlags, MaterialOverrideFlags::BaseColor);
    m_dirty = true;
}

void MaterialInstance::setMetallic(double metallic) {
    m_material.metallic = metallic;
    m_overrides.metallic = metallic;
    setFlag(m_overrideFlags, MaterialOverrideFlags::Metallic);
    m_dirty = true;
}

void MaterialInstance::setRoughness(double roughness) {
    m_material.roughness = roughness;
    m_overrides.roughness = roughness;
    setFlag(m_overrideFlags, MaterialOverrideFlags::Roughness);
    m_dirty = true;
}

void MaterialInstance::setAlpha(double alpha) {
    m_material.alpha = alpha;
    m_overrides.alpha = alpha;
    setFlag(m_overrideFlags, MaterialOverrideFlags::Alpha);
    m_dirty = true;
}

void MaterialInstance::setEmissive(const Vec3& color, double strength) {
    m_material.emissive = color;
    m_material.emissiveStrength = strength;
    m_overrides.emissive = color;
    m_overrides.emissiveStrength = strength;
    setFlag(m_overrideFlags, MaterialOverrideFlags::Emissive);
    setFlag(m_overrideFlags, MaterialOverrideFlags::EmissiveStrength);
    m_dirty = true;
}

void MaterialInstance::setDoubleSided(bool doubleSided) {
    m_material.doubleSided = doubleSided;
    m_overrides.doubleSided = doubleSided;
    setFlag(m_overrideFlags, MaterialOverrideFlags::DoubleSided);
    m_dirty = true;
}

void MaterialInstance::setAlphaCutoff(double cutoff) {
    m_material.alphaCutoff = cutoff;
    m_overrides.alphaCutoff = cutoff;
    setFlag(m_overrideFlags, MaterialOverrideFlags::AlphaCutoff);
    m_dirty = true;
}

void MaterialInstance::setSpecular(const Vec3& color) {
    m_material.specular = color;
    m_overrides.specular = color;
    setFlag(m_overrideFlags, MaterialOverrideFlags::Specular);
    m_dirty = true;
}

void MaterialInstance::setShininess(double shininess) {
    m_material.shininess = shininess;
    m_overrides.shininess = shininess;
    setFlag(m_overrideFlags, MaterialOverrideFlags::Shininess);
    m_dirty = true;
}

void MaterialInstance::setAO(double ao) {
    m_material.ao = ao;
    m_overrides.ao = ao;
    setFlag(m_overrideFlags, MaterialOverrideFlags::AO);
    m_dirty = true;
}

void MaterialInstance::setMaterialType(MaterialType type) {
    m_material.type = type;
    m_overrides.type = type;
    setFlag(m_overrideFlags, MaterialOverrideFlags::MaterialType);
    m_dirty = true;
}

void MaterialInstance::setAlphaMode(AlphaMode mode) {
    m_material.alphaMode = mode;
    m_overrides.alphaMode = mode;
    setFlag(m_overrideFlags, MaterialOverrideFlags::AlphaMode);
    m_dirty = true;
}

// --- 纹理设置 ---

void MaterialInstance::setAlbedoTexture(uint16_t textureId) {
    m_material.setAlbedoTexture(textureId);
    m_overrides.setAlbedoTexture(textureId);
    setFlag(m_overrideFlags, MaterialOverrideFlags::Textures);
    m_dirty = true;
}

void MaterialInstance::setNormalTexture(uint16_t textureId) {
    m_material.setNormalTexture(textureId);
    m_overrides.setNormalTexture(textureId);
    setFlag(m_overrideFlags, MaterialOverrideFlags::Textures);
    m_dirty = true;
}

void MaterialInstance::setMetallicRoughnessTexture(uint16_t textureId) {
    m_material.setMetallicRoughnessTexture(textureId);
    m_overrides.setMetallicRoughnessTexture(textureId);
    setFlag(m_overrideFlags, MaterialOverrideFlags::Textures);
    m_dirty = true;
}

void MaterialInstance::setEmissiveTexture(uint16_t textureId) {
    m_material.setEmissiveTexture(textureId);
    m_overrides.setEmissiveTexture(textureId);
    setFlag(m_overrideFlags, MaterialOverrideFlags::Textures);
    m_dirty = true;
}

void MaterialInstance::setAoTexture(uint16_t textureId) {
    m_material.setAoTexture(textureId);
    m_overrides.setAoTexture(textureId);
    setFlag(m_overrideFlags, MaterialOverrideFlags::Textures);
    m_dirty = true;
}

void MaterialInstance::setTextureBySlot(TextureSlot slot, uint16_t textureId) {
    m_material.setTextureBySlot(slot, textureId);
    m_overrides.setTextureBySlot(slot, textureId);
    setFlag(m_overrideFlags, MaterialOverrideFlags::Textures);
    m_dirty = true;
}

void MaterialInstance::setTexturePath(TextureSlot slot, const std::string& path) {
    size_t idx = static_cast<size_t>(slot);
    m_material.texturePaths[idx] = path;
    m_overrides.texturePaths[idx] = path;
    m_dirty = true;
}

// --- 便捷方法 ---

void MaterialInstance::setHighlight(const Vec3& highlightColor) {
    if (!m_highlighted) {
        m_savedMaterial = m_material;
        m_highlighted = true;
    }
    m_material.baseColor = highlightColor;
    m_material.emissive = highlightColor * 0.3;
    m_material.emissiveStrength = 1.0;
    m_dirty = true;
}

void MaterialInstance::clearHighlight() {
    if (m_highlighted) {
        m_material = m_savedMaterial;
        m_highlighted = false;
        m_dirty = true;
    }
}

void MaterialInstance::resetToTemplate() {
    if (m_templateId != 0) {
        auto* asset = MaterialCache::instance().findById(m_templateId);
        if (asset) {
            m_material = asset->get();
            m_overrideFlags = MaterialOverrideFlags::None;
            m_overrides = Material{};
            m_dirty = true;
            m_highlighted = false;
        }
    }
}

void MaterialInstance::resetOverrides() {
    m_overrideFlags = MaterialOverrideFlags::None;
    m_overrides = Material{};
    rebuildFromTemplate();
    m_dirty = true;
}

void MaterialInstance::rebuildFromTemplate() {
    if (m_templateId == 0) return;

    auto* asset = MaterialCache::instance().findById(m_templateId);
    if (!asset) return;

    Material base = asset->get();

    // 应用覆盖
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::BaseColor))
        base.baseColor = m_overrides.baseColor;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::Alpha))
        base.alpha = m_overrides.alpha;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::Metallic))
        base.metallic = m_overrides.metallic;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::Roughness))
        base.roughness = m_overrides.roughness;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::AO))
        base.ao = m_overrides.ao;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::Specular))
        base.specular = m_overrides.specular;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::Shininess))
        base.shininess = m_overrides.shininess;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::Emissive))
        base.emissive = m_overrides.emissive;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::EmissiveStrength))
        base.emissiveStrength = m_overrides.emissiveStrength;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::AlphaCutoff))
        base.alphaCutoff = m_overrides.alphaCutoff;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::DoubleSided))
        base.doubleSided = m_overrides.doubleSided;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::MaterialType))
        base.type = m_overrides.type;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::AlphaMode))
        base.alphaMode = m_overrides.alphaMode;
    if (hasFlag(m_overrideFlags, MaterialOverrideFlags::Textures))
        base.textures = m_overrides.textures;

    m_material = std::move(base);
    m_highlighted = false;
}

} // namespace MulanGeo::Engine
