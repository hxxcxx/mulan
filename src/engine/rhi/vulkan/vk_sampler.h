/**
 * @file vk_sampler.h
 * @brief Vulkan 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../sampler.h"
#include "vk_common.h"

namespace mulan::engine {

class VKSampler : public Sampler {
public:
    VKSampler(const SamplerDesc& desc, vk::Device device);
    ~VKSampler();

    const SamplerDesc& desc() const override { return desc_; }

    vk::Sampler handle() const { return sampler_; }

private:
    SamplerDesc desc_;
    vk::Device  device_;
    vk::Sampler sampler_;
};

} // namespace mulan::engine
