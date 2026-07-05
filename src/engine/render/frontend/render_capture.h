/**
 * @file render_capture.h
 * @brief RenderCaptureDesc/Result 定义截图与离屏读回的 frontend 数据协议。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../../rhi/texture.h"

#include <cstdint>
#include <vector>

namespace mulan::engine {

struct RenderCaptureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8_UNorm;
    bool readback = true;
};

struct RenderCaptureResult {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8_UNorm;
    std::vector<uint8_t> pixels;

    bool empty() const { return pixels.empty(); }
};

} // namespace mulan::engine
