#pragma once

#include <cstdint>
#include <string>

namespace mulan::graphics {

enum class AlphaMode : uint8_t {
    Opaque,
    Mask,
    Blend,
};

inline const char* alphaModeToString(AlphaMode m) {
    switch (m) {
    case AlphaMode::Opaque: return "Opaque";
    case AlphaMode::Mask:   return "Mask";
    case AlphaMode::Blend:  return "Blend";
    }
    return "Opaque";
}

inline AlphaMode alphaModeFromString(const std::string& s) {
    if (s == "Mask" || s == "mask")   return AlphaMode::Mask;
    if (s == "Blend" || s == "blend") return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

} // namespace mulan::graphics
