/**
 * @file render_target_info.h
 * @brief RenderTargetInfo 描述创建渲染管线所需的目标附件签名。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../../rhi/texture.h"

#include <cstdint>

namespace mulan::engine {

struct RenderTargetInfo {
    TextureFormat colorFormat = TextureFormat::Unknown;
    TextureFormat depthFormat = TextureFormat::Unknown;
    bool hasDepth = false;
    uint32_t sampleCount = 1;
};

}  // namespace mulan::engine
