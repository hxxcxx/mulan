/**
 * @file mesh_import_builder.h
 * @brief 网格导入构建器，统一创建材质、纹理、primitive 与文档实体。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include "import_result.h"

#include <mulan/asset/asset_id.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/core/result/error.h>
#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::io {

struct ImportedTextureDesc {
    std::string name;
    std::string sourcePath;
    bool srgb = true;
    int width = 0;
    int height = 0;
};

struct ImportedMaterialDesc {
    std::string name;
    math::Vec4 baseColorFactor{0.8, 0.8, 0.8, 1.0};
    double roughness = 0.5;
    double metallic = 0.0;
    asset::AssetId baseColorTexture = asset::AssetId::invalid();
    asset::AssetId normalTexture = asset::AssetId::invalid();
    asset::AssetId metallicRoughnessTexture = asset::AssetId::invalid();
    asset::AssetId emissiveTexture = asset::AssetId::invalid();
    asset::AssetId occlusionTexture = asset::AssetId::invalid();
    math::Vec3 emissiveFactor{0.0, 0.0, 0.0};
    asset::AlphaMode alphaMode = asset::AlphaMode::Opaque;
    bool doubleSided = false;
};

struct StandardMeshSource {
    std::span<const math::FVec3> positions;
    std::span<const math::FVec3> normals;
    std::span<const math::FVec2> texcoords;
    std::span<const uint32_t> indices;
    graphics::PrimitiveTopology topology = graphics::PrimitiveTopology::TriangleList;
    bool force32BitIndices = false;
};

graphics::Mesh buildStandardMesh(const StandardMeshSource& source);

struct ImportedMeshAsset {
    asset::AssetId geometry = asset::AssetId::invalid();
    std::vector<asset::AssetId> materialSlots;
    math::AABB3 bounds;
};

class MeshImportBuilder {
public:
    explicit MeshImportBuilder(Document& document);

    asset::AssetId createTexture(const ImportedTextureDesc& desc);
    asset::AssetId createMaterial(const ImportedMaterialDesc& desc);

    void addPrimitive(asset::MeshPrimitive primitive);
    void addPrimitive(graphics::Mesh mesh,
                      asset::AssetId material = asset::AssetId::invalid(),
                      std::string name = {});

    bool empty() const { return primitives_.empty(); }
    size_t primitiveCount() const { return primitives_.size(); }

    core::Result<ImportedMeshAsset> commitAsset(std::string name);
    core::Result<scene::EntityId> commit(std::string name);

    const ImportReport& report() const { return report_; }

private:
    Document& document_;
    std::vector<asset::MeshPrimitive> primitives_;
    ImportReport report_;
};

} // namespace mulan::io
