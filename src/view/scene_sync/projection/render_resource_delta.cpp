/**
 * @file render_resource_delta.cpp
 * @brief RenderWorld 持久 GPU 资源差量实现。
 * @author hxxcxx
 * @date 2026-07-21
 */

#include "render_resource_delta.h"

#include <mulan/graphics/mesh.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace mulan::view::detail {
namespace {

void hashValue(uint64_t& hash, uint64_t value) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    for (size_t byte = 0; byte < sizeof(value); ++byte) {
        hash ^= (value >> (byte * 8u)) & 0xffu;
        hash *= kFnvPrime;
    }
}

void hashBytes(uint64_t& hash, std::span<const std::byte> bytes) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    hashValue(hash, bytes.size());
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<uint8_t>(value);
        hash *= kFnvPrime;
    }
}

std::pair<uint64_t, uint64_t> meshContentFingerprint(const graphics::Mesh& mesh) {
    uint64_t primary = 14695981039346656037ull;
    uint64_t secondary = 7809847782465536322ull;
    const auto mixValue = [&](uint64_t value) {
        hashValue(primary, value);
        hashValue(secondary, value ^ 0x9e3779b97f4a7c15ull);
    };
    const auto mixBytes = [&](std::span<const std::byte> bytes) {
        hashBytes(primary, bytes);
        hashBytes(secondary, bytes);
    };

    mixValue(mesh.layout.stride());
    mixValue(mesh.layout.attrCount());
    mixValue(mesh.layout.bufferCount());
    for (const graphics::VertexAttribute& attribute : mesh.layout.attributes()) {
        mixValue(static_cast<uint64_t>(attribute.semantic));
        mixValue(static_cast<uint64_t>(attribute.format));
        mixValue(attribute.offset);
        mixValue(attribute.bufferSlot);
    }
    mixValue(static_cast<uint64_t>(mesh.indexType));
    mixValue(static_cast<uint64_t>(mesh.topology));
    mixBytes(mesh.vertices);
    mixBytes(mesh.indices);
    return { primary, secondary };
}

engine::RenderTextureResourceKey textureResourceKey(const engine::RenderTextureDesc& texture) {
    return engine::RenderTextureResourceKey{
        .resourceKey = texture.resourceKey,
        .srgb = texture.srgb,
        .generateMips = texture.generateMips,
    };
}

void collectTextureResource(TextureResourceCandidateMap& resources, const engine::RenderTextureDesc& texture) {
    const engine::RenderTextureResourceKey identity = textureResourceKey(texture);
    if (!identity || !texture.image || !texture.image->valid()) {
        return;
    }
    resources.insert_or_assign(identity, TextureResourceCandidate{
                                                 .image = texture.image,
                                                 .contentRevision = texture.contentRevision,
                                         });
}

bool resourceKeyLess(const engine::RenderResourceKey& lhs, const engine::RenderResourceKey& rhs) {
    if (lhs.domain.value != rhs.domain.value)
        return lhs.domain.value < rhs.domain.value;
    if (lhs.source != rhs.source)
        return lhs.source < rhs.source;
    if (lhs.subresource != rhs.subresource)
        return lhs.subresource < rhs.subresource;
    return lhs.kind < rhs.kind;
}

bool textureKeyLess(const engine::RenderTextureResourceKey& lhs, const engine::RenderTextureResourceKey& rhs) {
    if (lhs.resourceKey.domain.value != rhs.resourceKey.domain.value)
        return lhs.resourceKey.domain.value < rhs.resourceKey.domain.value;
    if (lhs.resourceKey.source != rhs.resourceKey.source)
        return lhs.resourceKey.source < rhs.resourceKey.source;
    if (lhs.resourceKey.subresource != rhs.resourceKey.subresource)
        return lhs.resourceKey.subresource < rhs.resourceKey.subresource;
    if (lhs.resourceKey.kind != rhs.resourceKey.kind)
        return lhs.resourceKey.kind < rhs.resourceKey.kind;
    if (lhs.srgb != rhs.srgb)
        return lhs.srgb < rhs.srgb;
    return lhs.generateMips < rhs.generateMips;
}

}  // namespace

void collectMaterialTextureResources(TextureResourceCandidateMap& resources,
                                     const engine::RenderMaterialDesc& material) {
    collectTextureResource(resources, material.baseColorTexture);
    collectTextureResource(resources, material.normalTexture);
    collectTextureResource(resources, material.metallicRoughnessTexture);
    collectTextureResource(resources, material.emissiveTexture);
    collectTextureResource(resources, material.ambientOcclusionTexture);
    collectTextureResource(resources, material.ambientTexture);
    collectTextureResource(resources, material.specularTexture);
    collectTextureResource(resources, material.shininessTexture);
    collectTextureResource(resources, material.opacityTexture);
}

void RenderResourceDelta::build(const GeometryResourceCandidateMap& geometries,
                                const TextureResourceCandidateMap& textures,
                                engine::RenderResourcePrepareList* prepare) {
    if (!prepare) {
        return;
    }

    std::vector<engine::RenderResourceKey> retiredGeometryKeys;
    retiredGeometryKeys.reserve(geometry_revisions_.size());
    for (const auto& [key, revision] : geometry_revisions_) {
        (void) revision;
        if (!geometries.contains(key))
            retiredGeometryKeys.push_back(key);
    }
    std::ranges::sort(retiredGeometryKeys, resourceKeyLess);
    for (const engine::RenderResourceKey key : retiredGeometryKeys)
        prepare->retireGeometry(key);

    std::vector<engine::RenderResourceKey> currentGeometryKeys;
    currentGeometryKeys.reserve(geometries.size());
    for (const auto& [key, resource] : geometries) {
        (void) resource;
        currentGeometryKeys.push_back(key);
    }
    std::ranges::sort(currentGeometryKeys, resourceKeyLess);

    GeometryRevisionMap nextGeometryRevisions;
    nextGeometryRevisions.reserve(geometries.size());
    for (const engine::RenderResourceKey key : currentGeometryKeys) {
        const GeometryResourceCandidate& resource = geometries.at(key);
        const auto previous = geometry_revisions_.find(key);
        const bool existed = previous != geometry_revisions_.end();
        GeometryContentRevision currentRevision{ resource.sourceRevision, 0, 0 };
        if (existed && previous->second[0] == resource.sourceRevision) {
            currentRevision = previous->second;
        } else {
            const auto [primary, secondary] = meshContentFingerprint(*resource.mesh);
            currentRevision[1] = primary;
            currentRevision[2] = secondary;
        }
        const bool contentChanged =
                existed && (previous->second[1] != currentRevision[1] || previous->second[2] != currentRevision[2]);
        if (force_full_prepare_ || !existed || contentChanged) {
            prepare->addGeometry(key, *resource.mesh, force_full_prepare_ || contentChanged);
        }
        nextGeometryRevisions.emplace(key, currentRevision);
    }
    geometry_revisions_ = std::move(nextGeometryRevisions);

    std::vector<engine::RenderTextureResourceKey> retiredTextureKeys;
    retiredTextureKeys.reserve(texture_revisions_.size());
    for (const auto& [key, revision] : texture_revisions_) {
        (void) revision;
        if (!textures.contains(key))
            retiredTextureKeys.push_back(key);
    }
    std::ranges::sort(retiredTextureKeys, textureKeyLess);
    for (const engine::RenderTextureResourceKey& key : retiredTextureKeys)
        prepare->retireTexture(key);

    std::vector<engine::RenderTextureResourceKey> currentTextureKeys;
    currentTextureKeys.reserve(textures.size());
    for (const auto& [key, resource] : textures) {
        (void) resource;
        currentTextureKeys.push_back(key);
    }
    std::ranges::sort(currentTextureKeys, textureKeyLess);

    TextureRevisionMap nextTextureRevisions;
    nextTextureRevisions.reserve(textures.size());
    for (const engine::RenderTextureResourceKey& key : currentTextureKeys) {
        const TextureResourceCandidate& resource = textures.at(key);
        const auto previous = texture_revisions_.find(key);
        const bool changed = previous == texture_revisions_.end() || previous->second != resource.contentRevision;
        if (force_full_prepare_ || changed)
            prepare->addTexture(key, resource.image, resource.contentRevision);
        nextTextureRevisions.emplace(key, resource.contentRevision);
    }
    texture_revisions_ = std::move(nextTextureRevisions);
    force_full_prepare_ = false;
}

void RenderResourceDelta::reset() {
    geometry_revisions_.clear();
    texture_revisions_.clear();
    force_full_prepare_ = true;
}

}  // namespace mulan::view::detail
