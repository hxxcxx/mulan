/**
 * @file render_material_resolver.h
 * @brief RenderMaterialResolver 将文档材质解析为渲染材质缓存偏移。
 * @date 2026-07-03
 */
#pragma once

#include <mulan/asset/asset_id.h>

#include <cstdint>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::engine {
class MaterialCache;
}

namespace mulan::view {

class RenderMaterialResolver {
public:
    explicit RenderMaterialResolver(const asset::AssetLibrary& assets)
        : assets_(assets) {}

    uint32_t materialOffset(asset::AssetId materialId, engine::MaterialCache& cache) const;

private:
    const asset::AssetLibrary& assets_;
};

} // namespace mulan::view