#include "import_builder.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/io/document.h>
#include <mulan/graphics/vertex/vertex_buffer.h>
#include <mulan/scene/scene.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>

namespace mulan::io {
namespace {

template <typename T>
void appendValue(std::vector<std::byte>& bytes, const T& value) {
    const auto oldSize = bytes.size();
    bytes.resize(oldSize + sizeof(T));
    std::memcpy(bytes.data() + oldSize, &value, sizeof(T));
}

std::vector<std::byte> readBinaryFile(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }

    const auto size = file.tellg();
    if (size <= 0) {
        return {};
    }

    std::vector<std::byte> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file) {
        return {};
    }
    return bytes;
}

}  // namespace

graphics::Mesh buildStandardMesh(const StandardMeshSource& source) {
    if (source.positions.empty())
        return {};

    graphics::Mesh mesh;
    const bool useTangents = source.tangents.size() >= source.positions.size();
    mesh.layout = useTangents ? graphics::layouts::pbr() : graphics::layouts::surface();
    mesh.topology = source.topology;

    graphics::VertexBufferBuilder vertices(mesh.layout, static_cast<uint32_t>(source.positions.size()));
    for (uint32_t i = 0; i < source.positions.size(); ++i) {
        const math::FVec3& position = source.positions[i];
        vertices.setPosition(i, position.x, position.y, position.z);

        const math::FVec3 normal = i < source.normals.size() ? source.normals[i] : math::FVec3(0.0f, 0.0f, 1.0f);
        vertices.setNormal(i, normal.x, normal.y, normal.z);

        const math::FVec2 uv = i < source.texcoords.size() ? source.texcoords[i] : math::FVec2(0.0f);
        float uvData[2] = { uv.x, uv.y };
        vertices.write(i, graphics::VertexSemantic::TexCoord0, uvData);

        if (useTangents) {
            const math::FVec4& tangent = source.tangents[i];
            float tangentData[4] = { tangent.x, tangent.y, tangent.z, tangent.w };
            vertices.write(i, graphics::VertexSemantic::Tangent, tangentData);
        }
    }

    auto vertexBytes = vertices.data();
    mesh.vertices.assign(vertexBytes.begin(), vertexBytes.end());

    const size_t indexCount = source.indices.empty() ? source.positions.size() : source.indices.size();
    const bool hasLargeIndex =
            std::any_of(source.indices.begin(), source.indices.end(), [](uint32_t index) { return index > 0xFFFFu; });
    const bool use32Bit = source.force32BitIndices || source.positions.size() > 0xFFFFu || hasLargeIndex;
    mesh.indexType = use32Bit ? graphics::IndexType::UInt32 : graphics::IndexType::UInt16;
    mesh.indices.reserve(indexCount * graphics::indexTypeSize(mesh.indexType));

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

ImportBuilder::ImportBuilder(Document& document) : document_(document) {
}

asset::AssetId ImportBuilder::createTexture(const ImportedTextureDesc& desc) {
    auto* library = document_.assets();
    if (!library)
        return asset::AssetId::invalid();

    auto* texture = library->create<asset::TextureAsset>(desc.name, desc.sourcePath);
    if (!texture)
        return asset::AssetId::invalid();

    std::vector<std::byte> encodedBytes = desc.data.empty() ? readBinaryFile(desc.sourcePath) : desc.data;
    std::shared_ptr<core::Image> image;
    if (!desc.data.empty()) {
        image = core::Image::loadFromMemory(reinterpret_cast<const uint8_t*>(desc.data.data()), desc.data.size());
    } else if (!desc.sourcePath.empty()) {
        image = core::Image::load(desc.sourcePath);
    }

    if (image && image->valid()) {
        texture->setImage(std::move(image));
    } else if (!desc.sourcePath.empty() || !desc.data.empty()) {
        const std::string source = desc.sourcePath.empty() ? desc.name : desc.sourcePath;
        report_.warnings.push_back("Failed to decode texture image: " + source);
    }

    if (!encodedBytes.empty()) {
        texture->setEmbeddedBytes(std::move(encodedBytes));
        texture->setMimeType(desc.mimeType);
    } else if (!desc.sourcePath.empty()) {
        report_.warnings.push_back("Failed to read texture bytes: " + desc.sourcePath);
    }
    if (desc.width > 0 && desc.height > 0) {
        texture->setSize(desc.width, desc.height);
    }
    ++report_.textureCount;
    return texture->id();
}

asset::AssetId ImportBuilder::createMaterial(const ImportedMaterialDesc& desc) {
    auto* library = document_.assets();
    if (!library)
        return asset::AssetId::invalid();

    auto* material = library->create<asset::MaterialAsset>(desc.name);
    if (!material)
        return asset::AssetId::invalid();

    material->setBaseColorFactor(desc.baseColorFactor);
    material->setRoughness(desc.roughness);
    material->setMetallic(desc.metallic);
    material->setBaseColorTexture(desc.baseColorTexture);
    material->setBaseColorTextureSrgb(desc.baseColorTextureSrgb);
    material->setNormalTexture(desc.normalTexture);
    material->setNormalTextureSrgb(desc.normalTextureSrgb);
    material->setMetallicRoughnessTexture(desc.metallicRoughnessTexture);
    material->setMetallicRoughnessTextureSrgb(desc.metallicRoughnessTextureSrgb);
    material->setEmissiveTexture(desc.emissiveTexture);
    material->setEmissiveTextureSrgb(desc.emissiveTextureSrgb);
    material->setOcclusionTexture(desc.occlusionTexture);
    material->setOcclusionTextureSrgb(desc.occlusionTextureSrgb);
    material->setAlphaMode(desc.alphaMode);
    material->setDoubleSided(desc.doubleSided);
    ++report_.materialCount;
    return material->id();
}

void ImportBuilder::addPrimitive(asset::MeshPrimitive primitive) {
    if (primitive.mesh.empty())
        return;
    primitives_.push_back(std::move(primitive));
    ++report_.primitiveCount;
}

void ImportBuilder::addPrimitive(graphics::Mesh mesh, asset::AssetId material, std::string name) {
    addPrimitive(asset::MeshPrimitive{ std::move(mesh), material, std::move(name) });
}

core::Result<ImportedMeshAsset> ImportBuilder::commitAsset(std::string name) {
    if (primitives_.empty()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Mesh import contains no primitives"));
    }

    auto* library = document_.assets();
    if (!library) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "Document has no asset library"));
    }

    auto* mesh = library->create<asset::MeshAsset>(std::move(name));
    if (!mesh) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "Failed to create mesh asset"));
    }

    ImportedMeshAsset result;
    result.geometry = mesh->id();
    result.bounds = math::AABB3::empty();
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

core::Result<scene::EntityId> ImportBuilder::commit(std::string name) {
    auto asset = commitAsset(name);
    if (!asset) {
        return std::unexpected(asset.error());
    }

    auto entity = document_.addSceneInstance(std::move(name), asset->geometry, std::move(asset->materialSlots));
    if (!entity.valid()) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "Failed to create mesh document entity"));
    }

    if (auto* scene = document_.scene()) {
        scene->setWorldBounds(entity, asset->bounds);
    }

    report_.entityCount += 1;
    return entity;
}

}  // namespace mulan::io
