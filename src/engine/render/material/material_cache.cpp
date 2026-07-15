#include "material_cache.h"

namespace mulan::engine {

// ============================================================
// MaterialCache
// ============================================================

MaterialCache::MaterialCache() {
    // 注册默认材质：index 0 = DefaultPBR（也是渲染端的回退目标）
    registerMaterial("DefaultPBR", Material::defaultPBR());             // index 0
    registerMaterial("DefaultPhong", Material::defaultPhong());         // index 1
    registerMaterial("Wireframe", Material::unlit({ 0.2, 0.2, 0.8 }));  // index 2
}

MaterialHandle MaterialCache::registerMaterial(Material material) {
    if (material.name.empty()) {
        material.name = "Material_" + std::to_string(materials_.size());
    }
    if (materials_.size() >= kMaxMaterials) {
        return kInvalidMaterialHandle;
    }
    const auto handle = materials_.size();
    name_to_index_[material.name] = handle;
    materials_.push_back(std::move(material));
    return handle;
}

MaterialHandle MaterialCache::registerMaterial(const std::string& name, Material material) {
    // 同名覆盖：句柄不变，仅替换数据 + 标脏
    auto it = name_to_index_.find(name);
    if (it != name_to_index_.end()) {
        const auto handle = it->second;
        material.name = name;
        if (materials_[handle] == material) {
            return handle;
        }
        materials_[handle] = std::move(material);
        return handle;
    }
    // 新增
    if (materials_.size() >= kMaxMaterials) {
        return kInvalidMaterialHandle;
    }
    material.name = name;
    const auto handle = materials_.size();
    name_to_index_[name] = handle;
    materials_.push_back(std::move(material));
    return handle;
}

const Material* MaterialCache::find(MaterialHandle handle) const {
    if (handle < materials_.size())
        return &materials_[handle];
    return nullptr;
}

Material* MaterialCache::find(MaterialHandle handle) {
    if (handle < materials_.size())
        return &materials_[handle];
    return nullptr;
}

const Material* MaterialCache::findByName(const std::string& name) const {
    auto it = name_to_index_.find(name);
    if (it != name_to_index_.end() && it->second < materials_.size()) {
        return &materials_[it->second];
    }
    return nullptr;
}

Material* MaterialCache::findByName(const std::string& name) {
    auto it = name_to_index_.find(name);
    if (it != name_to_index_.end() && it->second < materials_.size()) {
        return &materials_[it->second];
    }
    return nullptr;
}

void MaterialCache::forEach(const std::function<void(const Material&)>& fn) const {
    for (const auto& m : materials_) {
        fn(m);
    }
}

bool MaterialCache::updateMaterial(MaterialHandle handle, const Material& material) {
    if (handle >= materials_.size())
        return false;
    if (materials_[handle] == material)
        return true;
    materials_[handle] = material;
    return true;
}

bool MaterialCache::updateMaterial(const std::string& name, const Material& material) {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end())
        return false;
    const auto handle = it->second;
    if (materials_[handle] == material)
        return true;
    materials_[handle] = material;
    return true;
}

bool MaterialCache::remove(MaterialHandle handle) {
    if (handle >= materials_.size())
        return false;
    // 不删除默认材质(index 0-2)
    if (handle < 3)
        return false;
    materials_.erase(materials_.begin() + static_cast<std::ptrdiff_t>(handle));
    rebuildNameIndex();
    return true;
}

void MaterialCache::clear() {
    // 保留默认材质（前3个）
    size_t keepCount = std::min(materials_.size(), size_t(3));
    materials_.erase(materials_.begin() + static_cast<std::ptrdiff_t>(keepCount), materials_.end());
    rebuildNameIndex();
}

void MaterialCache::rebuildNameIndex() {
    name_to_index_.clear();
    for (size_t i = 0; i < materials_.size(); ++i) {
        if (!materials_[i].name.empty()) {
            name_to_index_[materials_[i].name] = i;
        }
    }
}

}  // namespace mulan::engine
