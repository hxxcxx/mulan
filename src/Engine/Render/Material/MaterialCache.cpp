/**
 * @file MaterialCache.cpp
 * @brief 材质缓存实现
 */

#include "MaterialCache.h"

namespace mulan::engine {

// ============================================================
// MaterialAsset
// ============================================================

MaterialAsset::MaterialAsset(Material material, uint32_t id)
    : m_material(std::move(material)), m_id(id) {}

// ============================================================
// MaterialCache
// ============================================================

MaterialCache& MaterialCache::instance() {
    static MaterialCache inst;
    return inst;
}

MaterialCache::MaterialCache() {
    init();
}

void MaterialCache::init() {
    // 注册默认材质
    registerMaterial("DefaultPBR", Material::defaultPBR());
    registerMaterial("DefaultPhong", Material::defaultPhong());
    registerMaterial("Wireframe", Material::unlit({0.2, 0.2, 0.8}));
}

uint32_t MaterialCache::registerMaterial(Material material) {
    auto id = allocateId();
    material.name = material.name.empty() ? ("Material_" + std::to_string(id)) : material.name;

    auto asset = std::make_unique<MaterialAsset>(std::move(material), id);
    const auto& name = asset->name();

    m_idToIndex[id] = m_materials.size();
    if (!name.empty()) {
        m_nameToIndex[name] = m_materials.size();
    }
    m_materials.push_back(std::move(asset));

    return id;
}

uint32_t MaterialCache::registerMaterial(const std::string& name, Material material) {
    // 检查是否已存在
    auto it = m_nameToIndex.find(name);
    if (it != m_nameToIndex.end()) {
        // 覆盖已有材质
        size_t idx = it->second;
        uint32_t existingId = m_materials[idx]->id();
        material.name = name;
        m_materials[idx] = std::make_unique<MaterialAsset>(std::move(material), existingId);
        return existingId;
    }

    auto id = allocateId();
    material.name = name;

    auto asset = std::make_unique<MaterialAsset>(std::move(material), id);

    m_nameToIndex[name] = m_materials.size();
    m_idToIndex[id] = m_materials.size();
    m_materials.push_back(std::move(asset));

    return id;
}

MaterialAsset* MaterialCache::defaultPBR() {
    return findByName("DefaultPBR");
}

MaterialAsset* MaterialCache::defaultPhong() {
    return findByName("DefaultPhong");
}

MaterialAsset* MaterialCache::wireframe() {
    return findByName("Wireframe");
}

MaterialAsset* MaterialCache::findById(uint32_t id) {
    auto it = m_idToIndex.find(id);
    if (it != m_idToIndex.end() && it->second < m_materials.size()) {
        return m_materials[it->second].get();
    }
    return nullptr;
}

MaterialAsset* MaterialCache::findByName(const std::string& name) {
    auto it = m_nameToIndex.find(name);
    if (it != m_nameToIndex.end() && it->second < m_materials.size()) {
        return m_materials[it->second].get();
    }
    return nullptr;
}

void MaterialCache::forEach(const std::function<void(const MaterialAsset&)>& fn) const {
    for (const auto& asset : m_materials) {
        fn(*asset);
    }
}

bool MaterialCache::updateMaterial(uint32_t id, const Material& material) {
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) {
        return false;
    }
    size_t idx = it->second;
    m_materials[idx] = std::make_unique<MaterialAsset>(material, id);
    return true;
}

bool MaterialCache::updateMaterial(const std::string& name, const Material& material) {
    auto it = m_nameToIndex.find(name);
    if (it == m_nameToIndex.end()) {
        return false;
    }
    size_t idx = it->second;
    auto id = m_materials[idx]->id();
    m_materials[idx] = std::make_unique<MaterialAsset>(material, id);
    return true;
}

bool MaterialCache::remove(uint32_t id) {
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) {
        return false;
    }

    size_t idx = it->second;
    m_materials.erase(m_materials.begin() + static_cast<std::ptrdiff_t>(idx));
    rebuildIndex();

    return true;
}

void MaterialCache::clear() {
    // 保留默认材质（前3个）
    size_t keepCount = std::min(m_materials.size(), size_t(3));
    m_materials.erase(m_materials.begin() + static_cast<std::ptrdiff_t>(keepCount),
                      m_materials.end());
    rebuildIndex();
}

uint32_t MaterialCache::allocateId() {
    return m_nextId++;
}

void MaterialCache::rebuildIndex() {
    m_idToIndex.clear();
    m_nameToIndex.clear();
    for (size_t i = 0; i < m_materials.size(); ++i) {
        m_idToIndex[m_materials[i]->id()] = i;
        const auto& n = m_materials[i]->name();
        if (!n.empty()) {
            m_nameToIndex[n] = i;
        }
    }
}

} // namespace mulan::Engine
