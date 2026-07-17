/**
 * @file render_capture.h
 * @brief RenderCaptureDesc/Result 定义截图尺寸、读回意图和实际输出数据。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../../rhi/texture.h"

#include <cstdint>
#include <vector>

namespace mulan::engine {

struct RenderCaptureDesc {
    /// 0 表示沿用当前窗口 Surface 的对应尺寸。
    uint32_t width = 0;
    uint32_t height = 0;
    /// false 只执行离屏渲染并返回输出元数据，不进行同步像素读回。
    bool readback = true;
};

struct RenderCaptureResult {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8_UNorm;
    uint32_t bytesPerPixel = 0;
    uint32_t rowBytes = 0;
    std::vector<uint8_t> pixels;

    bool empty() const { return pixels.empty(); }
};

}  // namespace mulan::engine
