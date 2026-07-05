#pragma once

#include "../../rhi/texture.h"

#include <cstdint>

namespace mulan::engine {

struct RenderTargetInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat colorFormat = TextureFormat::Unknown;
    TextureFormat depthFormat = TextureFormat::Unknown;
    bool hasDepth = false;
    bool presentable = false;
};

} // namespace mulan::engine
