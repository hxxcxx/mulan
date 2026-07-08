#pragma once

#include <mulan/asset/asset_id.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/engine/render/frontend/render_object.h>
#include <mulan/graphics/mesh.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mulan::view {

struct RenderItem {
    const graphics::Mesh* mesh = nullptr;
    asset::AssetId material = asset::AssetId::invalid();
    engine::RenderBucket bucket = engine::RenderBucket::Surface;
    uint64_t geometryKey = 0;
    size_t sourceDrawableIndex = 0;
};

struct RenderItemDiagnostics {
    size_t accepted = 0;
    size_t rejectedEmpty = 0;
    size_t rejectedTopology = 0;
    size_t rejectedLayout = 0;

    void reset() { *this = {}; }
};

class RenderItemBuilder {
public:
    static void buildSceneItems(asset::AssetId geometry, std::span<const asset::Drawable> drawables,
                                std::vector<RenderItem>& out, RenderItemDiagnostics* diagnostics = nullptr);

    static void buildPreviewItems(uint64_t generation, std::span<const graphics::Mesh> meshes,
                                  std::vector<RenderItem>& out, RenderItemDiagnostics* diagnostics = nullptr);

    static uint64_t previewMaterialKey();
};

}  // namespace mulan::view
