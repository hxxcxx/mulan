#include "material_cache.h"

namespace mulan::engine {

// ============================================================
// MaterialAsset
// ============================================================

MaterialAsset::MaterialAsset(Material material, uint32_t id)
    : material_(std::move(material)), id_(id) {}

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

    id_to_index_[id] = materials_.size();
    if (!name.empty()) {
        name_to_index_[name] = materials_.size();
    }
    materials_.push_back(std::move(asset));

    // 为新材质分配 UBO 偏移
    material_offsets_[id] = 0xFFFFFFFF; // 触发首次分配
    dirty_materials_.insert(id);

    return id;
}

uint32_t MaterialCache::registerMaterial(const std::string& name, Material material) {
    // 检查是否已存在
    auto it = name_to_index_.find(name);
    if (it != name_to_index_.end()) {
        // 覆盖已有材质
        size_t idx = it->second;
        uint32_t existingId = materials_[idx]->id();
        material.name = name;
        materials_[idx] = std::make_unique<MaterialAsset>(std::move(material), existingId);
        dirty_materials_.insert(existingId);
        return existingId;
    }

    auto id = allocateId();
    material.name = name;

    auto asset = std::make_unique<MaterialAsset>(std::move(material), id);

    name_to_index_[name] = materials_.size();
    id_to_index_[id] = materials_.size();
    materials_.push_back(std::move(asset));

    material_offsets_[id] = 0xFFFFFFFF;
    dirty_materials_.insert(id);

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
    auto it = id_to_index_.find(id);
    if (it != id_to_index_.end() && it->second < materials_.size()) {
        return materials_[it->second].get();
    }
    return nullptr;
}

MaterialAsset* MaterialCache::findByName(const std::string& name) {
    auto it = name_to_index_.find(name);
    if (it != name_to_index_.end() && it->second < materials_.size()) {
        return materials_[it->second].get();
    }
    return nullptr;
}

void MaterialCache::forEach(const std::function<void(const MaterialAsset&)>& fn) const {
    for (const auto& asset : materials_) {
        fn(*asset);
    }
}

bool MaterialCache::updateMaterial(uint32_t id, const Material& material) {
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) {
        return false;
    }
    size_t idx = it->second;
    materials_[idx] = std::make_unique<MaterialAsset>(material, id);
    dirty_materials_.insert(id);
    return true;
}

bool MaterialCache::updateMaterial(const std::string& name, const Material& material) {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end()) {
        return false;
    }
    size_t idx = it->second;
    auto id = materials_[idx]->id();
    materials_[idx] = std::make_unique<MaterialAsset>(material, id);
    dirty_materials_.insert(id);
    return true;
}

bool MaterialCache::remove(uint32_t id) {
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) {
        return false;
    }

    size_t idx = it->second;
    materials_.erase(materials_.begin() + static_cast<std::ptrdiff_t>(idx));
    rebuildIndex();

    return true;
}

void MaterialCache::clear() {
    // 保留默认材质（前3个）
    size_t keepCount = std::min(materials_.size(), size_t(3));
    materials_.erase(materials_.begin() + static_cast<std::ptrdiff_t>(keepCount),
                      materials_.end());
    rebuildIndex();
}

uint32_t MaterialCache::allocateId() {
    return next_id_++;
}

void MaterialCache::rebuildIndex() {
    id_to_index_.clear();
    name_to_index_.clear();
    for (size_t i = 0; i < materials_.size(); ++i) {
        id_to_index_[materials_[i]->id()] = i;
        const auto& n = materials_[i]->name();
        if (!n.empty()) {
            name_to_index_[n] = i;
        }
    }
}

// ============================================================
// GPU UBO 管理
// ============================================================

uint32_t MaterialCache::materialGpuOffset(uint32_t materialId) {
    // 0xFFFF = 默认材质，回退到 ID=1（DefaultPBR）
    if (materialId == 0xFFFF) {
        materialId = 1;
    }

    // 已分配 → 直接返回
    auto it = material_offsets_.find(materialId);
    if (it != material_offsets_.end() && it->second != 0xFFFFFFFF) {
        return it->second;
    }

    // 未分配 → 当场分配（首次查询时自动分配下一个可用 slot）
    uint32_t nextSlot = 0;
    for (auto& [id, offset] : material_offsets_) {
        if (offset != 0xFFFFFFFF) {
            uint32_t slotEnd = offset + kMaterialSlotStride;
            if (slotEnd > nextSlot) nextSlot = slotEnd;
        }
    }
    // 对齐到 kMaterialSlotStride
    nextSlot = ((nextSlot + kMaterialSlotStride - 1) / kMaterialSlotStride) * kMaterialSlotStride;

    material_offsets_[materialId] = nextSlot;
    dirty_materials_.insert(materialId);
    return nextSlot;
}

void MaterialCache::uploadDirtyMaterials(Buffer* materialUbo) {
    if (!materialUbo || dirty_materials_.empty()) return;

    // 对每个脏材质，分配 UBO 偏移（若尚未分配）并上传
    uint32_t nextSlot = 0;
    for (auto& asset : materials_) {
        uint32_t id = asset->id();
        if (material_offsets_[id] == 0xFFFFFFFF) {
            material_offsets_[id] = nextSlot * kMaterialSlotStride;
            ++nextSlot;
        }
    }

    for (uint32_t id : dirty_materials_) {
        uint32_t offset = material_offsets_[id];
        auto* asset = findById(id);
        if (!asset) continue;

        MaterialGPU gpu = asset->toGPU();
        materialUbo->update(offset, static_cast<uint32_t>(MaterialGPU::kSize), &gpu);
    }
}

void MaterialCache::clearDirtyMaterials() {
    dirty_materials_.clear();
}

} // namespace mulan::engine
