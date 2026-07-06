#include "asset_gpu_registry.h"
#include "../rhi/buffer.h"
#include "../rhi/device.h"

#include <cstddef>
#include <cstdint>

namespace mulan::engine {

AssetGpuRegistry::AssetGpuRegistry(RHIDevice& device) : device_(device) {
}

AssetGpuRegistry::GpuTextureResource::GpuTextureResource(std::unique_ptr<Texture> texture, std::string source)
    : texture(std::move(texture)), source(std::move(source)) {
    if (this->texture) {
        width = this->texture->width();
        height = this->texture->height();
    }
}

const GpuGeometry* AssetGpuRegistry::acquireGeometry(uint64_t key, const graphics::Mesh& mesh) {
    if (auto it = geometries_.find(key); it != geometries_.end()) {
        if (it->second.isValid()) {
            return &it->second;
        }

        auto result = createGpuBuffer(device_, mesh);
        if (!result) {
            return nullptr;
        }
        it->second = std::move(*result);
        return &it->second;
    }

    auto result = createGpuBuffer(device_, mesh);
    if (!result) {
        return nullptr;
    }
    auto [inserted, _] = geometries_.emplace(key, std::move(*result));
    return &inserted->second;
}

Texture* AssetGpuRegistry::acquireTextureFromFile(const std::string& path, const TextureLoadOptions& options) {
    if (path.empty()) {
        return nullptr;
    }

    const auto key = textureKey("file", path, options);
    if (auto it = textures_.find(key); it != textures_.end()) {
        return it->second.get();
    }

    TextureLoader loader;
    LoadedTexture loaded = loader.loadFromFile(path, options);
    if (loaded.pixels.empty()) {
        return nullptr;
    }

    const TextureUsageFlags usage = TextureUsageFlags::ShaderResource |
                                    (options.generateMips ? TextureUsageFlags::GenerateMips : TextureUsageFlags::None);
    auto texture = createRHITexture(loaded, usage, options.generateMips);
    if (!texture) {
        return nullptr;
    }

    auto [inserted, _] = textures_.emplace(key, GpuTextureResource{ std::move(texture), path });
    return inserted->second.get();
}

Texture* AssetGpuRegistry::acquireTextureFromMemory(const std::string& key, const std::byte* data, size_t size,
                                                    const TextureLoadOptions& options) {
    if (key.empty() || !data || size == 0) {
        return nullptr;
    }

    const auto cacheKey = textureKey("memory", key, options);
    if (auto it = textures_.find(cacheKey); it != textures_.end()) {
        return it->second.get();
    }

    TextureLoader loader;
    LoadedTexture loaded = loader.loadFromMemory(reinterpret_cast<const uint8_t*>(data), size, options);
    if (loaded.pixels.empty()) {
        return nullptr;
    }

    const TextureUsageFlags usage = TextureUsageFlags::ShaderResource |
                                    (options.generateMips ? TextureUsageFlags::GenerateMips : TextureUsageFlags::None);
    auto texture = createRHITexture(loaded, usage, options.generateMips);
    if (!texture) {
        return nullptr;
    }

    auto [inserted, _] = textures_.emplace(cacheKey, GpuTextureResource{ std::move(texture), key });
    return inserted->second.get();
}

Texture* AssetGpuRegistry::createTexture(uint32_t width, uint32_t height, TextureFormat format, TextureUsageFlags usage,
                                         const std::string& name) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.format = format;
    desc.dimension = TextureDimension::Texture2D;
    desc.usage = usage;

    auto result = device_.createTexture(desc);
    if (!result) {
        return nullptr;
    }

    auto texture = std::move(*result);
    const auto key = name.empty() ? ("generated:" + std::to_string(reinterpret_cast<uintptr_t>(texture.get())))
                                  : textureKey("generated", name, {});
    auto [inserted, _] = textures_.emplace(key, GpuTextureResource{ std::move(texture), name });
    return inserted->second.get();
}

void AssetGpuRegistry::clear() {
    geometries_.clear();
    textures_.clear();
}

core::Result<GpuGeometry> AssetGpuRegistry::createGpuBuffer(RHIDevice& device, const graphics::Mesh& mesh) {
    GpuGeometry geo;
    if (mesh.empty()) {
        return geo;
    }

    geo.layout = mesh.layout;
    geo.vertexStride = mesh.vertexStride();
    geo.vertexCount = mesh.vertexCount();
    geo.indexCount = mesh.indexCount();
    geo.indexType = mesh.indexType;

    if (geo.vertexCount > 0 && !mesh.vertices.empty()) {
        const uint32_t size = static_cast<uint32_t>(mesh.vertices.size());
        auto vb = device.createBuffer(BufferDesc::vertex(size, mesh.vertices.data(), "AssetGpuRegistryVB"));
        if (!vb) {
            return std::unexpected(vb.error());
        }
        geo.vertexBuffer = std::move(*vb);
    }

    if (geo.indexCount > 0 && !mesh.indices.empty()) {
        const uint32_t size = static_cast<uint32_t>(mesh.indices.size());
        auto ib = device.createBuffer(BufferDesc::index(size, mesh.indices.data(), "AssetGpuRegistryIB"));
        if (!ib) {
            return std::unexpected(ib.error());
        }
        geo.indexBuffer = std::move(*ib);
    }

    geo.uploaded = true;
    return geo;
}

std::string AssetGpuRegistry::textureKey(std::string_view sourceKind, const std::string& source,
                                         const TextureLoadOptions& options) {
    std::string key;
    key.reserve(sourceKind.size() + source.size() + 48);
    key.append(sourceKind);
    key.push_back(':');
    key.append(source);
    key.append("|srgb=");
    key.push_back(options.sRGB ? '1' : '0');
    key.append("|infer=");
    key.push_back(options.inferSrgbFromFile ? '1' : '0');
    key.append("|mips=");
    key.push_back(options.generateMips ? '1' : '0');
    key.append("|fmt=");
    key.append(std::to_string(static_cast<int>(options.format)));
    return key;
}

std::unique_ptr<Texture> AssetGpuRegistry::createRHITexture(const LoadedTexture& loaded, TextureUsageFlags usage,
                                                            bool generateMips) {
    TextureDesc desc;
    desc.width = static_cast<uint32_t>(loaded.width);
    desc.height = static_cast<uint32_t>(loaded.height);
    desc.depth = 1;
    desc.format = loaded.format;
    desc.dimension = TextureDimension::Texture2D;
    desc.usage = usage;

    auto result = device_.createTexture(desc);
    if (!result) {
        return nullptr;
    }

    if (!loaded.pixels.empty()) {
        device_.uploadTextureData(result->get(), loaded.pixels.data(), static_cast<uint32_t>(loaded.width),
                                  static_cast<uint32_t>(loaded.height), loaded.format);
    }

    // TODO: Generate the mip chain on GPU when RHIDevice exposes a portable path.
    (void) generateMips;

    return std::move(*result);
}

}  // namespace mulan::engine
