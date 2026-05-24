/**
 * @file TextureCache.cpp
 * @brief 纹理缓存实现
 */

#include "TextureCache.h"

#include <stdexcept>

namespace mulan::engine {

// ============================================================
// TextureAsset
// ============================================================

TextureAsset::TextureAsset(ResourcePtr<Texture> texture, std::string path)
    : m_texture(std::move(texture))
    , m_path(std::move(path)) {
    if (m_texture) {
        m_width  = m_texture->width();
        m_height = m_texture->height();
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
    m_device = device;
}

TextureAsset* TextureCache::load(const std::string& path,
                                 const TextureLoadOptions& options,
                                 bool /*async*/) {
    if (!m_device) {
        return nullptr;
    }

    // 检查是否已缓存
    auto it = m_textures.find(path);
    if (it != m_textures.end()) {
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
    auto [inserted, _] = m_textures.emplace(path, TextureAsset{std::move(texture), path});
    return &inserted->second;
}

TextureAsset* TextureCache::create(uint32_t width, uint32_t height,
                                    TextureFormat format,
                                    TextureUsageFlags usage,
                                    const std::string& name) {
    if (!m_device) {
        return nullptr;
    }

    TextureDesc desc;
    desc.width     = width;
    desc.height    = height;
    desc.depth     = 1;
    desc.format    = format;
    desc.dimension  = TextureDimension::Texture2D;
    desc.usage     = usage;

    auto texture = m_device->createTexture(desc);
    if (!texture) {
        return nullptr;
    }

    auto key = name.empty() ? ("__unnamed_" + std::to_string(reinterpret_cast<uintptr_t>(texture.get()))) : name;
    auto [inserted, _] = m_textures.emplace(key, TextureAsset{std::move(texture), key});
    return &inserted->second;
}

TextureAsset* TextureCache::find(const std::string& name) {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        return &it->second;
    }
    return nullptr;
}

bool TextureCache::remove(const std::string& name) {
    return m_textures.erase(name) > 0;
}

void TextureCache::clear() {
    m_textures.clear();
}

std::vector<std::string> TextureCache::allNames() const {
    std::vector<std::string> names;
    names.reserve(m_textures.size());
    for (const auto& [name, _] : m_textures) {
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

    auto texture = m_device->createTexture(desc);
    if (!texture) {
        return nullptr;
    }

    // 上传像素数据
    // 注意：实际应该用 upload buffer，这里简化处理
    // TODO: 实现 proper 的 upload context
    (void)generateMips;

    return texture;
}

} // namespace mulan::Engine
