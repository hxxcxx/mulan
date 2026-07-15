#include "asset_gpu_registry.h"
#include "../rhi/buffer.h"
#include "../rhi/device.h"
#include "../rhi/engine_error_code.h"

#include <mulan/core/log/log.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace mulan::engine {

AssetGpuRegistry::AssetGpuRegistry(RHIDevice& device) : device_(device) {
}

AssetGpuRegistry::GpuTextureResource::GpuTextureResource(std::unique_ptr<Texture> texture, std::string source,
                                                         uint64_t contentRevision)
    : texture(std::move(texture)), source(std::move(source)), contentRevision(contentRevision) {
    if (this->texture) {
        width = this->texture->width();
        height = this->texture->height();
    }
}

core::Result<const GpuGeometry*> AssetGpuRegistry::acquireGeometry(AssetGpuKey key, const graphics::Mesh& mesh,
                                                                   bool forceUpdate) {
    if (!key) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "GPU geometry resource key is invalid."));
    }

    if (auto it = geometries_.find(key); it != geometries_.end()) {
        if (!forceUpdate) {
            return &it->second;
        }

        auto result = createGpuBuffer(mesh);
        if (!result) {
            return std::unexpected(result.error());
        }
        GpuGeometry oldGeometry = std::move(it->second);
        it->second = std::move(*result);
        if (oldGeometry.isValid()) {
            if (auto retired = retireGeometryResource(std::move(oldGeometry)); !retired) {
                return std::unexpected(retired.error());
            }
        }
        return &it->second;
    }

    auto result = createGpuBuffer(mesh);
    if (!result) {
        return std::unexpected(result.error());
    }
    auto [inserted, _] = geometries_.emplace(key, std::move(*result));
    return &inserted->second;
}

core::Result<bool> AssetGpuRegistry::retireGeometry(AssetGpuKey key) {
    if (!key) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "GPU geometry resource key is invalid."));
    }

    auto node = geometries_.extract(key);
    if (node.empty()) {
        return false;
    }

    GpuGeometry geometry = std::move(node.mapped());
    if (geometry.isValid()) {
        if (auto retired = retireGeometryResource(std::move(geometry)); !retired) {
            return std::unexpected(retired.error());
        }
    }
    return true;
}

core::Result<Texture*> AssetGpuRegistry::acquireTexture(AssetGpuKey key, const core::Image& image,
                                                        const TextureLoadOptions& options, uint64_t contentRevision) {
    if (!key || !image.valid()) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "GPU texture resource input is invalid."));
    }

    const auto cacheKey = textureKey(key, options);
    if (auto it = textures_.find(cacheKey); it != textures_.end()) {
        if (it->second.contentRevision == contentRevision) {
            return it->second.get();
        }

        const TextureUsageFlags usage =
                TextureUsageFlags::ShaderResource | TextureUsageFlags::TransferDst |
                (options.generateMips ? TextureUsageFlags::GenerateMips : TextureUsageFlags::None);
        auto texture = createRHITexture(image, usage, options.sRGB, options.generateMips);
        if (!texture) {
            return std::unexpected(texture.error());
        }

        GpuTextureResource oldResource = std::move(it->second);
        it->second = GpuTextureResource{ std::move(*texture), std::to_string(key.value), contentRevision };
        if (oldResource.texture) {
            if (auto retired = retireTextureResource(std::move(oldResource.texture)); !retired) {
                return std::unexpected(retired.error());
            }
        }
        return it->second.get();
    }

    const TextureUsageFlags usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::TransferDst |
                                    (options.generateMips ? TextureUsageFlags::GenerateMips : TextureUsageFlags::None);
    auto texture = createRHITexture(image, usage, options.sRGB, options.generateMips);
    if (!texture) {
        return std::unexpected(texture.error());
    }

    auto [inserted, _] = textures_.emplace(
            cacheKey, GpuTextureResource{ std::move(*texture), std::to_string(key.value), contentRevision });
    return inserted->second.get();
}

core::Result<bool> AssetGpuRegistry::retireTexture(AssetGpuKey key, const TextureLoadOptions& options) {
    if (!key) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "GPU texture resource key is invalid."));
    }

    const auto cacheKey = textureKey(key, options);
    auto it = textures_.find(cacheKey);
    if (it == textures_.end()) {
        return false;
    }

    GpuTextureResource resource = std::move(it->second);
    textures_.erase(it);
    if (resource.texture) {
        if (auto retired = retireTextureResource(std::move(resource.texture)); !retired) {
            return std::unexpected(retired.error());
        }
    }
    return true;
}

void AssetGpuRegistry::releaseUploadFailureKeepalives() {
    failed_upload_geometry_.reset();
    failed_upload_texture_.reset();
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
    textures_.clear();
    retirement_failure_keepalive_.reset();
    retirement_failure_texture_keepalive_.reset();
    failed_upload_geometry_.reset();
    failed_upload_texture_.reset();
}

core::Result<GpuGeometry> AssetGpuRegistry::createGpuBuffer(const graphics::Mesh& mesh) {
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
        auto vb = device_.createBuffer(BufferDesc::vertex(size, mesh.vertices.data(), "AssetGpuRegistryVB"));
        if (!vb) {
            return std::unexpected(vb.error());
        }
        geo.vertexBuffer = std::move(*vb);
    }

    if (geo.indexCount > 0 && !mesh.indices.empty()) {
        const uint32_t size = static_cast<uint32_t>(mesh.indices.size());
        auto ib = device_.createBuffer(BufferDesc::index(size, mesh.indices.data(), "AssetGpuRegistryIB"));
        if (!ib) {
            // vertexBuffer 的上传命令可能已经录入当前批次；保活到调用方 flush 完成。
            if (geo.vertexBuffer) {
                failed_upload_geometry_.emplace(std::move(geo));
            }
            return std::unexpected(ib.error());
        }
        geo.indexBuffer = std::move(*ib);
    }

    geo.uploaded = true;
    return geo;
}

core::Result<void> AssetGpuRegistry::retireGeometryResource(GpuGeometry geometry) {
    const SubmissionToken token = device_.lastSubmissionToken();
    if (!token) {
        return {};
    }

    auto keepalive = std::make_shared<GpuGeometry>(std::move(geometry));
    auto retired = device_.retire(token, [keepalive] {});
    if (!retired) {
        // 理论上同一 device 产生的 token 不应被拒绝；保守保活并让上层 fail-stop，
        // 避免退役登记异常退化为旧帧仍在使用的 GPU 对象提前析构。
        retirement_failure_keepalive_.emplace(std::move(*keepalive));
        return std::unexpected(retired.error());
    }
    return {};
}

core::Result<void> AssetGpuRegistry::retireTextureResource(std::unique_ptr<Texture> texture) {
    if (!texture) {
        return {};
    }
    const SubmissionToken token = device_.lastSubmissionToken();
    if (!token) {
        return {};
    }

    auto keepalive = std::shared_ptr<Texture>(std::move(texture));
    auto retired = device_.retire(token, [keepalive] {});
    if (!retired) {
        // 与几何退役一样，token 异常时宁可 fail-stop 并保活，
        // 也不允许旧帧正在采样的贴图提前析构。
        retirement_failure_texture_keepalive_ = std::move(keepalive);
        return std::unexpected(retired.error());
    }
    return {};
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

core::Result<std::unique_ptr<Texture>> AssetGpuRegistry::createRHITexture(const core::Image& image,
                                                                          TextureUsageFlags usage, bool sRGB,
                                                                          bool generateMips) {
    std::shared_ptr<core::Image> rgbaImage;
    const core::Image* uploadImage = &image;
    if (image.format() != core::PixelFormat::RGBA8) {
        rgbaImage = image.toRGBA();
        if (!rgbaImage || !rgbaImage->valid()) {
            return std::unexpected(
                    makeError(EngineErrorCode::ResourceUploadFailed, "Texture conversion to RGBA failed."));
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
        return std::unexpected(result.error());
    }

    if (uploadImage->data() && uploadImage->totalBytes() > 0) {
        auto uploadResult = device_.uploadTextureData(
                result->get(),
                TextureUploadDesc::tightlyPacked(std::span(uploadImage->data(), uploadImage->totalBytes()),
                                                 uploadImage->width(), uploadImage->height(), desc.format));
        if (!uploadResult) {
            LOG_ERROR("[AssetGpuRegistry] Texture upload failed: {}", uploadResult.error().message);
            // RHI 不要求失败前完全没有录制命令；保活目标纹理直到批次 flush 收口。
            failed_upload_texture_ = std::move(*result);
            return std::unexpected(uploadResult.error());
        }
    }

    // TODO: Generate the mip chain on GPU when RHIDevice exposes a portable path.
    (void) generateMips;

    return std::move(*result);
}

}  // namespace mulan::engine
