/**
 * @file texture_cache.h
 * @brief 纹理资产管理器
 * @author hxxcxx
 * @date 2026-04-23
 */

#pragma once

#include "../rhi/texture.h"
#include "../rhi/device.h"
#include "texture_loader.h"

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
    explicit TextureAsset(std::unique_ptr<Texture> texture, std::string path = {});

    // 禁用拷贝，纹理独占
    TextureAsset(const TextureAsset&) = delete;
    TextureAsset& operator=(const TextureAsset&) = delete;

    // 支持移动
    TextureAsset(TextureAsset&&) noexcept = default;
    TextureAsset& operator=(TextureAsset&&) noexcept = default;

    Texture* get() { return texture_.get(); }
    const Texture* get() const { return texture_.get(); }
    Texture& operator*() { return *texture_; }
    const Texture& operator*() const { return *texture_; }

    explicit operator bool() const { return texture_ != nullptr; }

    const std::string& path() const { return path_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    std::unique_ptr<Texture> texture_;
    std::string path_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

// ============================================================
// 纹理缓存 — 单例，管理所有纹理资产
// ============================================================

class TextureCache {
public:
    /// 构造时注入 RHI device（非单例，由 Renderer 持有）
    explicit TextureCache(RHIDevice* device) : device_(device) {}
    ~TextureCache() = default;

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    /// 从文件加载纹理（带缓存）
    /// 如果已加载过，直接返回已有 asset
    TextureAsset* load(const std::string& path, const TextureLoadOptions& options = {}, bool async = false);

    /// 创建空白纹理
    TextureAsset* create(uint32_t width, uint32_t height, TextureFormat format, TextureUsageFlags usage,
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
    size_t size() const { return textures_.size(); }

    /// 是否为空
    bool empty() const { return textures_.empty(); }

private:
    // 内部：从 LoadedTexture 创建 RHI Texture
    std::unique_ptr<Texture> createRHITexture(const LoadedTexture& loaded, TextureUsageFlags usage, bool generateMips);

    RHIDevice* device_ = nullptr;
    std::unordered_map<std::string, TextureAsset> textures_;
};

}  // namespace mulan::engine
