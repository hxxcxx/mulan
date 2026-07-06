/**
 * @file font_manager.h
 * @brief Renderer-owned font atlas manager.
 * @author hxxcxx
 * @date 2026-06-30
 */

#pragma once

#include "font_atlas.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace mulan::engine {

class RHIDevice;

class FontManager {
public:
    explicit FontManager(RHIDevice& device);
    ~FontManager() = default;

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    /// 加载字体
    /// @param key       字体标识（如 "default", "consolas", "simhei"）
    /// @param fontPath  TTF/OTF 文件路径
    /// @param fontSize  MSDF 基准字号（像素，默认 48）
    /// @param atlasSize 图集尺寸（默认 1024）
    /// @return 成功返回 true
    bool loadFont(const char* key, const char* fontPath, float fontSize = 48.0f, uint32_t atlasSize = 1024);

    /// 按 key 获取字体，未找到返回 nullptr
    FontAtlas* font(const char* key) const;

    /// 获取默认字体（第一个加载的）
    FontAtlas* defaultFont() const;

    /// 是否已初始化
    bool hasFonts() const { return !fonts_.empty(); }

private:
    RHIDevice* device_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<FontAtlas>> fonts_;
    std::string default_key_;
};

}  // namespace mulan::engine
