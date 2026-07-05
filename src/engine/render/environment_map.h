/**
 * @file environment_map.h
 * @brief IBL 烘焙管线 — 把 equirectangular HDR 转成 PBR 三件套（equirect 2D 版本）
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 启动时一次性烘焙：
 *   - irradiance_ : 2D RGBA16F equirect（漫反射 IBL，N 方向半球卷积）
 *   - prefilter_  : 2D RGBA16F equirect（镜面 IBL，单档 roughness GGX 卷积）
 *   - brdfLUT_    : 2D RG16F 512²（split-sum BRDF 积分）
 *
 * 全部基于 equirectangular 2D 纹理，不依赖 cube，RHI 无需任何改动。
 * 烘焙失败（HDR 加载失败 / 资源创建失败）返回 false，调用方走 fallback。
 */
#pragma once

#include "../rhi/texture.h"
#include "../rhi/device.h"

#include <memory>
#include <string>

namespace mulan::engine {

class RHIDevice;
class Sampler;

class IBLPipeline {
public:
    static constexpr uint32_t kIrradianceW = 256;
    static constexpr uint32_t kIrradianceH = 128;
    static constexpr uint32_t kPrefilterW = 512;
    static constexpr uint32_t kPrefilterH = 256;
    static constexpr uint32_t kBrdfLUTSize = 512;

    IBLPipeline() = default;
    ~IBLPipeline();

    IBLPipeline(const IBLPipeline&) = delete;
    IBLPipeline& operator=(const IBLPipeline&) = delete;

    /// 加载 equirect HDR 并烘焙三件套。失败返回 false（调用方静默降级）。
    bool bake(RHIDevice& device, const std::string& hdrPath);

    bool isValid() const { return irradiance_ != nullptr; }

    Texture* irradiance() const { return irradiance_.get(); }
    Texture* prefilter() const { return prefilter_.get(); }
    Texture* brdfLUT() const { return brdf_lut_.get(); }

private:
    std::unique_ptr<Texture> irradiance_;
    std::unique_ptr<Texture> prefilter_;
    std::unique_ptr<Texture> brdf_lut_;
    std::unique_ptr<Sampler> linear_sampler_;  // 仅 bake 期间持有
};

}  // namespace mulan::engine
