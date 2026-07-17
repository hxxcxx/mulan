/**
 * @file draw_fallback_resources.h
 * @brief 管理单个 RHI Device 上共享的绘制兜底纹理与采样器。
 * @author hxxcxx
 * @date 2026-07-18
 *
 * 这些资源不属于场景、材质或视图，而是 Device 级绘制契约的一部分：
 * 当材质未提供对应纹理，或环境光照尚未生成时，保证所有声明的 binding
 * 始终拥有有效资源。实例由 DeviceResourceService 唯一持有，绘制阶段只借用。
 */

#pragma once

#include "../../rhi/sampler.h"
#include "../../rhi/texture.h"

#include <mulan/core/result/error.h>

#include <memory>

namespace mulan::engine {

class RHIDevice;

class DrawFallbackResources {
public:
    explicit DrawFallbackResources(RHIDevice& device);

    DrawFallbackResources(const DrawFallbackResources&) = delete;
    DrawFallbackResources& operator=(const DrawFallbackResources&) = delete;

    /// 创建全部兜底资源。只有全部成功后才会发布，重复调用为幂等操作。
    ResultVoid init();

    Texture* whiteTexture() const { return initialized_ ? white_texture_.get() : nullptr; }
    Texture* blackTexture() const { return initialized_ ? black_texture_.get() : nullptr; }
    Texture* normalTexture() const { return initialized_ ? normal_texture_.get() : nullptr; }
    Texture* metallicRoughnessTexture() const { return initialized_ ? metallic_roughness_texture_.get() : nullptr; }
    Texture* environmentTexture() const { return initialized_ ? environment_texture_.get() : nullptr; }
    Texture* brdfLutTexture() const { return initialized_ ? brdf_lut_texture_.get() : nullptr; }
    Sampler* sampler() const { return initialized_ ? sampler_.get() : nullptr; }

private:
    RHIDevice& device_;
    std::unique_ptr<Sampler> sampler_;
    std::unique_ptr<Texture> white_texture_;
    std::unique_ptr<Texture> black_texture_;
    std::unique_ptr<Texture> normal_texture_;
    std::unique_ptr<Texture> metallic_roughness_texture_;
    std::unique_ptr<Texture> environment_texture_;
    std::unique_ptr<Texture> brdf_lut_texture_;
    bool initialized_ = false;
};

}  // namespace mulan::engine
