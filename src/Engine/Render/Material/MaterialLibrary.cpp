/**
 * @file MaterialLibrary.cpp
 * @brief 预设材质库实现
 */

#include "MaterialLibrary.h"

namespace MulanGeo::engine {

// ============================================================
// MaterialLibrary
// ============================================================

MaterialLibrary& MaterialLibrary::instance() {
    static MaterialLibrary inst;
    return inst;
}

void MaterialLibrary::init() {
    registerBuiltinPresets();
}

void MaterialLibrary::registerBuiltinPresets() {
    struct Preset {
        const char* name;
        Material (*factory)();
    };

    static const Preset kPresets[] = {
        { "Steel",         &MaterialLibrary::steel },
        { "Copper",        &MaterialLibrary::copper },
        { "Gold",          &MaterialLibrary::gold },
        { "Aluminum",      &MaterialLibrary::aluminum },
        { "Plastic_White", &MaterialLibrary::plastic_white },
        { "Plastic_Red",   &MaterialLibrary::plastic_red },
        { "Plastic_Blue",  &MaterialLibrary::plastic_blue },
        { "Plastic_Green", &MaterialLibrary::plastic_green },
        { "Rubber",        &MaterialLibrary::rubber },
        { "Glass",         &MaterialLibrary::glass },
        { "Concrete",      &MaterialLibrary::concrete },
        { "Wood",          &MaterialLibrary::wood },
        { "Marble",        &MaterialLibrary::marble },
        { "Ceramic",       &MaterialLibrary::ceramic },
        { "Emissive_White",&MaterialLibrary::emissive_white },
        { "Emissive_Red",  &MaterialLibrary::emissive_red },
        { "Emissive_Blue", &MaterialLibrary::emissive_blue },
    };

    auto& cache = MaterialCache::instance();
    for (const auto& p : kPresets) {
        Material m = p.factory();
        m.name = p.name;
        cache.registerMaterial(p.name, std::move(m));
        m_presets.push_back(p.name);
    }
}

size_t MaterialLibrary::loadPresetsFromFile(const std::string& path) {
    std::vector<Material> materials;
    auto result = MaterialSerializer::loadCollection(path, materials);
    if (!result.success) return 0;

    auto& cache = MaterialCache::instance();
    size_t loaded = 0;
    for (auto& m : materials) {
        if (!m.name.empty()) {
            cache.registerMaterial(m.name, std::move(m));
            m_presets.push_back(m.name);
            loaded++;
        }
    }
    return loaded;
}

uint32_t MaterialLibrary::registerPreset(Material material) {
    auto name = material.name;
    auto id = MaterialCache::instance().registerMaterial(name, std::move(material));
    m_presets.push_back(std::move(name));
    return id;
}

MaterialAsset* MaterialLibrary::find(const std::string& name) {
    return MaterialCache::instance().findByName(name);
}

std::vector<std::string> MaterialLibrary::presetNames() const {
    return m_presets;
}

void MaterialLibrary::clear() {
    auto& cache = MaterialCache::instance();
    for (const auto& name : m_presets) {
        auto* asset = cache.findByName(name);
        if (asset) cache.remove(asset->id());
    }
    m_presets.clear();
}

// ============================================================
// 内置预设工厂
// ============================================================

Material MaterialLibrary::steel() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.56, 0.57, 0.58};
    m.metallic = 0.9;
    m.roughness = 0.35;
    return m;
}

Material MaterialLibrary::copper() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.72, 0.45, 0.20};
    m.metallic = 0.95;
    m.roughness = 0.25;
    return m;
}

Material MaterialLibrary::gold() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {1.0, 0.76, 0.34};
    m.metallic = 1.0;
    m.roughness = 0.2;
    return m;
}

Material MaterialLibrary::aluminum() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.91, 0.92, 0.92};
    m.metallic = 0.95;
    m.roughness = 0.15;
    return m;
}

Material MaterialLibrary::plastic_white() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.95, 0.95, 0.95};
    m.metallic = 0.0;
    m.roughness = 0.4;
    return m;
}

Material MaterialLibrary::plastic_red() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.85, 0.12, 0.10};
    m.metallic = 0.0;
    m.roughness = 0.4;
    return m;
}

Material MaterialLibrary::plastic_blue() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.10, 0.20, 0.85};
    m.metallic = 0.0;
    m.roughness = 0.4;
    return m;
}

Material MaterialLibrary::plastic_green() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.10, 0.65, 0.15};
    m.metallic = 0.0;
    m.roughness = 0.4;
    return m;
}

Material MaterialLibrary::rubber() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.15, 0.15, 0.15};
    m.metallic = 0.0;
    m.roughness = 0.9;
    return m;
}

Material MaterialLibrary::glass() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.95, 0.95, 0.98};
    m.metallic = 0.0;
    m.roughness = 0.05;
    m.alpha = 0.3;
    m.alphaMode = AlphaMode::Blend;
    m.doubleSided = true;
    return m;
}

Material MaterialLibrary::concrete() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.55, 0.53, 0.50};
    m.metallic = 0.0;
    m.roughness = 0.85;
    return m;
}

Material MaterialLibrary::wood() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.65, 0.45, 0.25};
    m.metallic = 0.0;
    m.roughness = 0.7;
    return m;
}

Material MaterialLibrary::marble() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.90, 0.88, 0.86};
    m.metallic = 0.0;
    m.roughness = 0.3;
    return m;
}

Material MaterialLibrary::ceramic() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.92, 0.90, 0.88};
    m.metallic = 0.0;
    m.roughness = 0.2;
    return m;
}

Material MaterialLibrary::emissive_white() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {1.0, 1.0, 1.0};
    m.emissive = {1.0, 1.0, 1.0};
    m.emissiveStrength = 2.0;
    m.metallic = 0.0;
    m.roughness = 0.5;
    return m;
}

Material MaterialLibrary::emissive_red() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {1.0, 0.2, 0.1};
    m.emissive = {1.0, 0.2, 0.1};
    m.emissiveStrength = 2.0;
    m.metallic = 0.0;
    m.roughness = 0.5;
    return m;
}

Material MaterialLibrary::emissive_blue() {
    Material m;
    m.type = MaterialType::PBR;
    m.baseColor = {0.1, 0.3, 1.0};
    m.emissive = {0.1, 0.3, 1.0};
    m.emissiveStrength = 2.0;
    m.metallic = 0.0;
    m.roughness = 0.5;
    return m;
}

} // namespace MulanGeo::Engine
