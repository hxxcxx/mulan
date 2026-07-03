#include "mesh_import_builder.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/document/document.h>
#include <mulan/engine/vertex/vertex_buffer.h>
#include <mulan/scene/scene.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace mulan::io {
namespace {

template<typename T>
void appendValue(std::vector<std::byte>& bytes, const T& value) {
    const auto oldSize = bytes.size();
    bytes.resize(oldSize + sizeof(T));
    std::memcpy(bytes.data() + oldSize, &value, sizeof(T));
}

} // namespace

engine::Mesh buildStandardMesh(const StandardMeshSource& source) {
    if (source.positions.empty()) return {};

    engine::Mesh mesh;
    mesh.layout = engine::layouts::surface();
    mesh.topology = source.topology;

    engine::VertexBufferBuilder vertices(mesh.layout,
                                         static_cast<uint32_t>(source.positions.size()));
    for (uint32_t i = 0; i < source.positions.size(); ++i) {
        const engine::FVec3& position = source.positions[i];
        vertices.setPosition(i, position.x, position.y, position.z);

        const engine::FVec3 normal =
            i < source.normals.size() ? source.normals[i] : engine::FVec3(0.0f, 0.0f, 1.0f);
        vertices.setNormal(i, normal.x, normal.y, normal.z);

        const engine::FVec2 uv = i < source.texcoords.size() ? source.texcoords[i] : engine::FVec2(0.0f);
        float uvData[2] = {uv.x, uv.y};
        vertices.write(i, engine::VertexSemantic::TexCoord0, uvData);
    }

    auto vertexBytes = vertices.data();
    mesh.vertices.assign(vertexBytes.begin(), vertexBytes.end());

    const size_t indexCount = source.indices.empty() ? source.positions.size() : source.indices.size();
    const bool hasLargeIndex = std::any_of(source.indices.begin(), source.indices.end(),
                                           [](uint32_t index) { return index > 0xFFFFu; });
    const bool use32Bit = source.force32BitIndices || source.positions.size() > 0xFFFFu
                       || hasLargeIndex;
    mesh.indexType = use32Bit ? engine::IndexType::UInt32 : engine::IndexType::UInt16;
    mesh.indices.reserve(indexCount * engine::indexTypeSize(mesh.indexType));

    for (size_t i = 0; i < indexCount; ++i) {
        const uint32_t index = source.indices.empty() ? static_cast<uint32_t>(i) : source.indices[i];
        if (use32Bit) {
            appendValue(mesh.indices, index);
        } else {
            appendValue(mesh.indices, static_cast<uint16_t>(index));
        }
    }

    mesh.computeBounds();
    return mesh;
}

MeshImportBuilder::MeshImportBuilder(document::Document& document)
    : document_(document)
{}

asset::AssetId MeshImportBuilder::createTexture(const ImportedTextureDesc& desc) {
    auto* library = document_.assets();
    if (!library) return asset::AssetId::invalid();

    auto* texture = library->create<asset::TextureAsset>(desc.name, desc.sourcePath);
    if (!texture) return asset::AssetId::invalid();

    texture->setSrgb(desc.srgb);
    texture->setSize(desc.width, desc.height);
    ++report_.textureCount;
    return texture->id();
}

asset::AssetId MeshImportBuilder::createMaterial(const ImportedMaterialDesc& desc) {
    auto* library = document_.assets();
    if (!library) return asset::AssetId::invalid();

    auto* material = library->create<asset::MaterialAsset>(desc.name);
    if (!material) return asset::AssetId::invalid();

    material->setBaseColorFactor(desc.baseColorFactor);
    material->setRoughness(desc.roughness);
    material->setMetallic(desc.metallic);
    material->setBaseColorTexture(desc.baseColorTexture);
    material->setNormalTexture(desc.normalTexture);
    material->setMetallicRoughnessTexture(desc.metallicRoughnessTexture);
    material->setAlphaMode(desc.alphaMode);
    material->setDoubleSided(desc.doubleSided);
    ++report_.materialCount;
    return material->id();
}

void MeshImportBuilder::addPrimitive(asset::MeshPrimitive primitive) {
    if (primitive.mesh.empty()) return;
    primitives_.push_back(std::move(primitive));
    ++report_.primitiveCount;
}

void MeshImportBuilder::addPrimitive(engine::Mesh mesh, asset::AssetId material, std::string name) {
    addPrimitive(asset::MeshPrimitive{std::move(mesh), material, std::move(name)});
}

std::expected<ImportedMeshAsset, core::Error> MeshImportBuilder::commitAsset(std::string name) {
    if (primitives_.empty()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg,
                                                "Mesh import contains no primitives"));
    }

    auto* library = document_.assets();
    if (!library) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal,
                                                "Document has no asset library"));
    }

    auto* mesh = library->create<asset::MeshAsset>(std::move(name));
    if (!mesh) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal,
                                                "Failed to create mesh asset"));
    }

    ImportedMeshAsset result;
    result.geometry = mesh->id();
    result.bounds = engine::AABB::empty();
    result.materialSlots.reserve(primitives_.size());

    for (auto& primitive : primitives_) {
        primitive.mesh.computeBounds();
        if (!primitive.mesh.bounds.isEmpty()) {
            result.bounds.expand(primitive.mesh.bounds);
        }
        result.materialSlots.push_back(primitive.material);
        mesh->addPrimitive(std::move(primitive.mesh), primitive.material, std::move(primitive.name));
    }

    report_.meshAssetCount += 1;
    primitives_.clear();
    return result;
}

std::expected<scene::EntityId, core::Error> MeshImportBuilder::commit(std::string name) {
    auto asset = commitAsset(name);
    if (!asset) {
        return std::unexpected(asset.error());
    }

    auto entity = document_.addSceneInstance(std::move(name),
                                             asset->geometry,
                                             std::move(asset->materialSlots));
    if (!entity.valid()) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal,
                                                "Failed to create mesh document entity"));
    }

    if (auto* scene = document_.scene()) {
        scene->setWorldBounds(entity, asset->bounds);
    }

    report_.entityCount += 1;
    return entity;
}

} // namespace mulan::io
