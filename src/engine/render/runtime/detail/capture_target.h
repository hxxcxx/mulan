/**
 * @file capture_target.h
 * @brief 截图目标，独占离屏 RenderTarget 与可选的 CPU 回读缓冲。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <mulan/core/result/error.h>
#include "../../../rhi/buffer.h"
#include "../../../rhi/render_target.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mulan::engine {
class RHIDevice;
}

namespace mulan::engine::detail {

struct CaptureTargetDesc {
    int width = 0;
    int height = 0;
    engine::TextureFormat colorFormat = engine::TextureFormat::RGBA8_UNorm;
    engine::TextureFormat depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;
    bool hasDepth = true;
    uint32_t sampleCount = 1;
    bool readback = true;
};

class CaptureTarget {
public:
    explicit CaptureTarget(engine::RHIDevice& device);
    ~CaptureTarget();

    CaptureTarget(const CaptureTarget&) = delete;
    CaptureTarget& operator=(const CaptureTarget&) = delete;

    ResultVoid configure(const CaptureTargetDesc& desc);
    ResultVoid readbackPixels(std::vector<uint8_t>& pixels);
    void shutdown();

    bool isConfigured() const { return render_target_ != nullptr; }
    engine::RenderTarget& renderTarget() const;

    uint32_t bytesPerPixel() const { return bytes_per_pixel_; }
    uint32_t rowBytes() const { return row_bytes_; }
    engine::TextureFormat colorFormat() const;

private:
    bool descMatches(const CaptureTargetDesc& desc) const;

    engine::RHIDevice& device_;
    std::unique_ptr<engine::RenderTarget> render_target_;
    std::unique_ptr<engine::Buffer> readback_buffer_;
    CaptureTargetDesc desc_;
    uint32_t bytes_per_pixel_ = 0;
    uint32_t row_bytes_ = 0;
};

}  // namespace mulan::engine::detail
