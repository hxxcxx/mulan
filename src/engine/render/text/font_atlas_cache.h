/**
 * @file font_atlas_cache.h
 * @brief MSDF 字体图集缓存
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "font_atlas.h"

#include <cstdint>
#include <filesystem>

namespace mulan::engine {

struct FontAtlasCacheKey {
    uint64_t keyHash = 0;
    uint64_t fontContentHash = 0;
    uint64_t charsetHash = 0;
    uint64_t fontFileSize = 0;
    float fontSize = 48.0f;
    uint32_t requestedAtlasWidth = 1024;
    uint32_t requestedAtlasHeight = 1024;
    std::filesystem::path fontPath;
    std::filesystem::path filePath;
};

class FontAtlasCache {
public:
    static FontAtlasCacheKey makeKey(const char* fontPath, float fontSize, uint32_t atlasWidth, uint32_t atlasHeight,
                                     uint64_t charsetHash);

    static bool tryLoad(const FontAtlasCacheKey& key, FontAtlasCpuData& outData);
    static bool save(const FontAtlasCacheKey& key, const FontAtlasCpuData& data);

private:
    static std::filesystem::path cacheDirectory();
};

}  // namespace mulan::engine
