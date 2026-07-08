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

const GpuGeometry* AssetGpuRegistry::acquireGeometry(AssetGpuKey key, const graphics::Mesh& mesh, bool forceUpdate) {
    if (!key) {
        return nullptr;
    }

    if (auto it = geometries_.find(key); it != geometries_.end()) {
        if (!forceUpdate) {
            return &it->second;
        }

        auto result = createGpuBuffer(device_, mesh);
        if (!result) {
            return nullptr;
        }
        if (it->second.isValid()) {
            retired_geometries_.push_back(std::move(it->second));
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

Texture* AssetGpuRegistry::acquireTexture(AssetGpuKey key, const core::Image& image,
                                          const TextureLoadOptions& options) {
    if (!key || !image.valid()) {
        return nullptr;
    }

    const auto cacheKey = textureKey(key, options);
    if (auto it = textures_.find(cacheKey); it != textures_.end()) {
        return it->second.get();
    }

    const TextureUsageFlags usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::TransferDst |
                                    (options.generateMips ? TextureUsageFlags::GenerateMips : TextureUsageFlags::None);
    auto texture = createRHITexture(image, usage, options.sRGB, options.generateMips);
    if (!texture) {
        return nullptr;
    }

    auto [inserted, _] =
            textures_.emplace(cacheKey, GpuTextureResource{ std::move(texture), std::to_string(key.value) });
    return inserted->second.get();
}

const GpuGeometry* AssetGpuRegistry::findGeometry(AssetGpuKey key) const {
    if (!key) {
        return nullptr;
    }
    auto it = geometries_.find(key);
    return it != geometries_.end() && it->second.isValid() ? &it->second : nullptr;
}

Texture* AssetGpuRegistry::findTexture(AssetGpuKey key, const TextureLoadOptions& options) {
    if (!key) {
        return nullptr;
    }
    const auto cacheKey = textureKey(key, options);
    auto it = textures_.find(cacheKey);
    return it != textures_.end() ? it->second.get() : nullptr;
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
                                  : ("generated:" + name);
    auto [inserted, _] = textures_.emplace(key, GpuTextureResource{ std::move(texture), name });
    return inserted->second.get();
}

void AssetGpuRegistry::clear() {
    geometries_.clear();
    retired_geometries_.clear();
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

std::string AssetGpuRegistry::textureKey(AssetGpuKey resourceKey, const TextureLoadOptions& options) {
    std::string key;
    key.reserve(64);
    key.append("asset:");
    key.append(std::to_string(resourceKey.value));
    key.append("|srgb=");
    key.push_back(options.sRGB ? '1' : '0');
    key.append("|mips=");
    key.push_back(options.generateMips ? '1' : '0');
    return key;
}

TextureFormat AssetGpuRegistry::toRHITextureFormat(core::PixelFormat pixelFmt, bool sRGB) {
    switch (pixelFmt) {
    case core::PixelFormat::R8: return TextureFormat::R8_UNorm;
    case core::PixelFormat::RG8: return TextureFormat::RGBA8_UNorm;
    case core::PixelFormat::RGB8:
    case core::PixelFormat::RGBA8: return sRGB ? TextureFormat::RGBA8_sRGB : TextureFormat::RGBA8_UNorm;
    default: return sRGB ? TextureFormat::RGBA8_sRGB : TextureFormat::RGBA8_UNorm;
    }
}

std::unique_ptr<Texture> AssetGpuRegistry::createRHITexture(const core::Image& image, TextureUsageFlags usage,
                                                            bool sRGB, bool generateMips) {
    std::shared_ptr<core::Image> rgbaImage;
    const core::Image* uploadImage = &image;
    if (image.format() != core::PixelFormat::RGBA8) {
        rgbaImage = image.toRGBA();
        if (!rgbaImage || !rgbaImage->valid()) {
            return nullptr;
        }
        uploadImage = rgbaImage.get();
    }

    TextureDesc desc;
    desc.width = uploadImage->width();
    desc.height = uploadImage->height();
    desc.depth = 1;
    desc.format = toRHITextureFormat(uploadImage->format(), sRGB);
    desc.dimension = TextureDimension::Texture2D;
    desc.usage = usage;

    auto result = device_.createTexture(desc);
    if (!result) {
        return nullptr;
    }

    if (uploadImage->data() && uploadImage->totalBytes() > 0) {
        device_.uploadTextureData(result->get(), uploadImage->data(), uploadImage->width(), uploadImage->height(),
                                  desc.format);
    }

    // TODO: Generate the mip chain on GPU when RHIDevice exposes a portable path.
    (void) generateMips;

    return std::move(*result);
}

}  // namespace mulan::engine
