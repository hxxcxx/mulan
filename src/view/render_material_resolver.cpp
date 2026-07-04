#include "render_material_resolver.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/material_asset.h>
#include <mulan/engine/render/material/material.h>
#include <mulan/engine/render/material/material_cache.h>

#include <string>

namespace mulan::view {
namespace {

engine::Material toEngineMaterial(const asset::MaterialAsset& asset) {
    engine::Material material = engine::Material::defaultPBR();
    material.name = asset.name();
    const auto& color = asset.baseColorFactor();
    material.baseColor = {color.x, color.y, color.z};
    material.alpha = color.w;
    material.metallic = asset.metallic();
    material.roughness = asset.roughness();
    material.alphaMode = asset.alphaMode();
    material.doubleSided = asset.doubleSided();
    return material;
}

} // namespace

uint32_t RenderMaterialResolver::materialOffset(asset::AssetId materialId,
                                                engine::MaterialCache& cache) const {
    if (!materialId) {
        return cache.materialGpuOffset(0);  // 默认材质 index 0
    }

    const auto* materialAsset = dynamic_cast<const asset::MaterialAsset*>(assets_.asset(materialId));
    if (!materialAsset) {
        return cache.materialGpuOffset(0);
    }

    const std::string cacheName = "asset:" + std::to_string(materialId.value);
    const auto handle = cache.registerMaterial(cacheName, toEngineMaterial(*materialAsset));
    return cache.materialGpuOffset(handle);
}

} // namespace mulan::view