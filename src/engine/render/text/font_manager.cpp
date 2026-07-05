#include "font_manager.h"

namespace mulan::engine {

FontManager& FontManager::instance() {
    static FontManager inst;
    return inst;
}

bool FontManager::loadFont(const char* key, const char* fontPath, float fontSize, uint32_t atlasSize) {
    if (!device_)
        return false;

    auto atlas = std::make_unique<FontAtlas>(device_);
    if (!atlas->load(fontPath, fontSize, atlasSize, atlasSize)) {
        return false;
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
