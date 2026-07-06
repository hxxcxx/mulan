/**
 * @file render_target_info.h
 * @brief RenderTargetInfo 描述当前帧输出目标的尺寸、格式和呈现属性。
 * @author hxxcxx
 * @date 2026-07-05
 */

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
    uint32_t sampleCount = 1;
};

}  // namespace mulan::engine
