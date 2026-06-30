/**
 * @file FontManager.cpp
 * @brief 字体管理器实现
 * @author hxxcxx
 * @date 2026-06-30
 */

#include "FontManager.h"

namespace mulan::engine {

FontManager& FontManager::instance() {
    static FontManager inst;
    return inst;
}

bool FontManager::loadFont(const char* key, const char* fontPath,
                            float fontSize, uint32_t atlasSize) {
    if (!m_device) return false;

    auto atlas = std::make_unique<FontAtlas>(m_device);
    if (!atlas->load(fontPath, fontSize, atlasSize, atlasSize)) {
        return false;
    }

    if (m_defaultKey.empty()) {
        m_defaultKey = key;
    }

    m_fonts[key] = std::move(atlas);
    return true;
}

FontAtlas* FontManager::font(const char* key) const {
    auto it = m_fonts.find(key);
    return (it != m_fonts.end()) ? it->second.get() : nullptr;
}

FontAtlas* FontManager::defaultFont() const {
    if (m_defaultKey.empty()) return nullptr;
    return font(m_defaultKey.c_str());
}

} // namespace mulan::engine
