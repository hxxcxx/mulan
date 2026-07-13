#include "font_manager.h"

#include "font_atlas_cache.h"

#include <mulan/core/log/log.h>

#include <utility>

namespace mulan::engine {

FontManager::FontManager(RHIDevice& device) : device_(&device) {
}

bool FontManager::loadFont(const char* key, const char* fontPath, float fontSize, uint32_t atlasSize) {
    if (!device_)
        return false;

    const uint64_t charsetHash = FontAtlas::defaultCharsetHash();
    const FontAtlasCacheKey cacheKey = FontAtlasCache::makeKey(fontPath, fontSize, atlasSize, atlasSize, charsetHash);

    auto atlas = std::make_unique<FontAtlas>(device_);
    FontAtlasCpuData cpuData;
    if (FontAtlasCache::tryLoad(cacheKey, cpuData)) {
        LOG_DEBUG("[FontAtlasCache] Cache hit: {}", cacheKey.filePath.string());
        if (!atlas->loadFromCpuData(std::move(cpuData))) {
            return false;
        }
    } else {
        LOG_DEBUG("[FontAtlasCache] Cache miss: {}", cacheKey.filePath.string());
        if (!FontAtlas::generateCpuData(fontPath, fontSize, atlasSize, atlasSize, cpuData)) {
            return false;
        }
        if (!FontAtlasCache::save(cacheKey, cpuData)) {
            LOG_WARN("[FontAtlasCache] Failed to save cache: {}", cacheKey.filePath.string());
        }
        if (!atlas->loadFromCpuData(std::move(cpuData))) {
            return false;
        }
    }

    if (default_key_.empty()) {
        default_key_ = key;
    }

    fonts_[key] = std::move(atlas);
    return true;
}

FontAtlas* FontManager::font(const char* key) const {
    auto it = fonts_.find(key);
    return (it != fonts_.end()) ? it->second.get() : nullptr;
}

FontAtlas* FontManager::defaultFont() const {
    if (default_key_.empty())
        return nullptr;
    return font(default_key_.c_str());
}

}  // namespace mulan::engine
