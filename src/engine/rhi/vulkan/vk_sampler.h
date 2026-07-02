/**
 * @file vk_sampler.h
 * @brief Vulkan 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../sampler.h"
#include "vk_common.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class VKSampler : public Sampler {
public:
    /// 创建 VKSampler。失败返回 SamplerCreateFailed。
    static std::expected<std::unique_ptr<VKSampler>, core::Error>
        create(const SamplerDesc& desc, vk::Device device);
    ~VKSampler();

    const SamplerDesc& desc() const override { return desc_; }

    vk::Sampler handle() const { return sampler_; }

private:
    VKSampler(const SamplerDesc& desc, vk::Device device)
        : desc_(desc), device_(device) {}

    SamplerDesc desc_;
    vk::Device  device_;
    vk::Sampler sampler_;
};

} // namespace mulan::engine
