/**
 * @file VKSampler.h
 * @brief Vulkan 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../Sampler.h"
#include "VkCommon.h"

namespace mulan::engine {

class VKSampler : public Sampler {
public:
    VKSampler(const SamplerDesc& desc, vk::Device device);
    ~VKSampler();

    const SamplerDesc& desc() const override { return m_desc; }

    vk::Sampler handle() const { return m_sampler; }

private:
    SamplerDesc m_desc;
    vk::Device  m_device;
    vk::Sampler m_sampler;
};

} // namespace mulan::engine
