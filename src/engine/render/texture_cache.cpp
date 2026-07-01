#include "texture_cache.h"

#include <stdexcept>

namespace mulan::engine {

// ============================================================
// TextureAsset
// ============================================================

TextureAsset::TextureAsset(ResourcePtr<Texture> texture, std::string path)
    : texture_(std::move(texture))
    , path_(std::move(path)) {
    if (texture_) {
        width_  = texture_->width();
        height_ = texture_->height();
    }
}

// ============================================================
// TextureCache
// ============================================================

TextureCache& TextureCache::instance() {
    static TextureCache inst;
    return inst;
}

void TextureCache::init(RHIDevice* device) {
    device_ = device;
}

TextureAsset* TextureCache::load(const std::string& path,
                                 const TextureLoadOptions& options,
                                 bool /*async*/) {
    if (!device_) {
        return nullptr;
    }

    // 检查是否已缓存
    auto it = textures_.find(path);
    if (it != textures_.end()) {
        return &it->second;
    }

    // 加载图片数据
    TextureLoader loader;
    LoadedTexture loaded = loader.loadFromFile(path, options);
    if (loaded.pixels.empty()) {
        return nullptr;
    }

    // 创建 RHI Texture
    TextureDesc desc;
    desc.width      = static_cast<uint32_t>(loaded.width);
    desc.height     = static_cast<uint32_t>(loaded.height);
    desc.depth      = 1;
    desc.format     = loaded.format;
    desc.dimension   = TextureDimension::Texture2D;
    desc.usage      = TextureUsageFlags::ShaderResource | (options.generateMips ? TextureUsageFlags::GenerateMips : TextureUsageFlags::None);

    auto texture = createRHITexture(loaded, desc.usage, options.generateMips);
    if (!texture) {
        return nullptr;
    }

    // 插入缓存
    auto [inserted, _] = textures_.emplace(path, TextureAsset{std::move(texture), path});
    return &inserted->second;
}

TextureAsset* TextureCache::create(uint32_t width, uint32_t height,
                                    TextureFormat format,
                                    TextureUsageFlags usage,
                                    const std::string& name) {
    if (!device_) {
        return nullptr;
    }

    TextureDesc desc;
    desc.width     = width;
    desc.height    = height;
    desc.depth     = 1;
    desc.format    = format;
    desc.dimension  = TextureDimension::Texture2D;
    desc.usage     = usage;

    auto texture = device_->createTexture(desc);
    if (!texture) {
        return nullptr;
    }

    auto key = name.empty() ? ("__unnamed_" + std::to_string(reinterpret_cast<uintptr_t>(texture.get()))) : name;
    auto [inserted, _] = textures_.emplace(key, TextureAsset{std::move(texture), key});
    return &inserted->second;
}

TextureAsset* TextureCache::find(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool TextureCache::remove(const std::string& name) {
    return textures_.erase(name) > 0;
}

void TextureCache::clear() {
    textures_.clear();
}

std::vector<std::string> TextureCache::allNames() const {
    std::vector<std::string> names;
    names.reserve(textures_.size());
    for (const auto& [name, _] : textures_) {
        names.push_back(name);
    }
    return names;
}

ResourcePtr<Texture> TextureCache::createRHITexture(const LoadedTexture& loaded,
                                                     TextureUsageFlags usage,
                                                     bool generateMips) {
    TextureDesc desc;
    desc.width     = static_cast<uint32_t>(loaded.width);
    desc.height    = static_cast<uint32_t>(loaded.height);
    desc.depth     = 1;
    desc.format    = loaded.format;
    desc.dimension = TextureDimension::Texture2D;
    desc.usage     = usage;

    auto texture = device_->createTexture(desc);
    if (!texture) {
        return nullptr;
    }

    // 上传像素数据
    // 注意：实际应该用 upload buffer，这里简化处理
    // TODO: 实现 proper 的 upload context
    (void)generateMips;

    return texture;
}

} // namespace mulan::engine
