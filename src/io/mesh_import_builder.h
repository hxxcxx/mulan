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
#include <mulan/engine/geometry/mesh.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace mulan::document {
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
    engine::Vec4 baseColorFactor{0.8, 0.8, 0.8, 1.0};
    double roughness = 0.5;
    double metallic = 0.0;
    asset::AssetId baseColorTexture = asset::AssetId::invalid();
    asset::AssetId normalTexture = asset::AssetId::invalid();
    asset::AssetId metallicRoughnessTexture = asset::AssetId::invalid();
    asset::AlphaMode alphaMode = asset::AlphaMode::Opaque;
    bool doubleSided = false;
};

struct StandardMeshSource {
    std::span<const engine::FVec3> positions;
    std::span<const engine::FVec3> normals;
    std::span<const glm::vec2> texcoords;
    std::span<const uint32_t> indices;
    engine::PrimitiveTopology topology = engine::PrimitiveTopology::TriangleList;
    bool force32BitIndices = false;
};

engine::Mesh buildStandardMesh(const StandardMeshSource& source);

class MeshImportBuilder {
public:
    explicit MeshImportBuilder(document::Document& document);

    asset::AssetId createTexture(const ImportedTextureDesc& desc);
    asset::AssetId createMaterial(const ImportedMaterialDesc& desc);

    void addPrimitive(asset::MeshPrimitive primitive);
    void addPrimitive(engine::Mesh mesh,
                      asset::AssetId material = asset::AssetId::invalid(),
                      std::string name = {});

    bool empty() const { return primitives_.empty(); }
    size_t primitiveCount() const { return primitives_.size(); }

    std::expected<scene::EntityId, core::Error> commit(std::string name);

    const ImportReport& report() const { return report_; }

private:
    document::Document& document_;
    std::vector<asset::MeshPrimitive> primitives_;
    ImportReport report_;
};

} // namespace mulan::io
