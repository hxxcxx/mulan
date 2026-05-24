/**
 * @file TextureCache.h
 * @brief 纹理资产管理器
 * @author hxxcxx
 * @date 2026-04-23
 */

#pragma once

#include "../RHI/Texture.h"
#include "../RHI/Device.h"
#include "TextureLoader.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::engine {

// ============================================================
// 纹理资产 — 持有 RHI Texture 资源
// ============================================================

class TextureAsset {
public:
    TextureAsset() = default;
    explicit TextureAsset(ResourcePtr<Texture> texture, std::string path = {});

    // 禁用拷贝，纹理独占
    TextureAsset(const TextureAsset&) = delete;
    TextureAsset& operator=(const TextureAsset&) = delete;

    // 支持移动
    TextureAsset(TextureAsset&&) noexcept = default;
    TextureAsset& operator=(TextureAsset&&) noexcept = default;

    Texture*        get()       { return m_texture.get(); }
    const Texture*  get() const { return m_texture.get(); }
    Texture&        operator*()       { return *m_texture; }
    const Texture&  operator*() const { return *m_texture; }

    explicit operator bool() const { return m_texture != nullptr; }

    const std::string& path() const { return m_path; }
    uint32_t width()  const { return m_width; }
    uint32_t height() const { return m_height; }

private:
    ResourcePtr<Texture> m_texture;
    std::string          m_path;
    uint32_t             m_width  = 0;
    uint32_t             m_height = 0;
};

// ============================================================
// 纹理缓存 — 单例，管理所有纹理资产
// ============================================================

class TextureCache {
public:
    /// 获取全局实例
    static TextureCache& instance();

    /// 初始化（需要 RHIDevice）
    void init(RHIDevice* device);

    /// 从文件加载纹理（带缓存）
    /// 如果已加载过，直接返回已有 asset
    TextureAsset* load(const std::string& path,
                       const TextureLoadOptions& options = {},
                       bool async = false);

    /// 创建空白纹理
    TextureAsset* create(uint32_t width, uint32_t height,
                         TextureFormat format,
                         TextureUsageFlags usage,
                         const std::string& name = {});

    /// 按名称查找
    TextureAsset* find(const std::string& name);

    /// 移除纹理
    bool remove(const std::string& name);

    /// 清空所有纹理
    void clear();

    /// 获取所有纹理名称
    std::vector<std::string> allNames() const;

    /// 纹理数量
    size_t size() const { return m_textures.size(); }

    /// 是否为空
    bool empty() const { return m_textures.empty(); }

private:
    TextureCache()  = default;
    ~TextureCache() = default;

    // 内部：从 LoadedTexture 创建 RHI Texture
    ResourcePtr<Texture> createRHITexture(const LoadedTexture& loaded,
                                           TextureUsageFlags usage,
                                           bool generateMips);

    RHIDevice*                    m_device = nullptr;
    std::unordered_map<std::string, TextureAsset> m_textures;
};

} // namespace mulan::Engine
