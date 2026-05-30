/**
 * @file VKSampler.cpp
 * @brief Vulkan 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#include "VKSampler.h"
#include "VKConvert.h"
#include <cstdio>

namespace mulan::engine {

// ============================================================
// Helper: RHI enum → Vulkan enum
// ============================================================

static vk::Filter toVkFilter(SamplerFilter f) {
    switch (f) {
    case SamplerFilter::Nearest:     return vk::Filter::eNearest;
    case SamplerFilter::Linear:      return vk::Filter::eLinear;
    case SamplerFilter::Anisotropic: return vk::Filter::eLinear;  // aniso 用 anisotropyEnable 控制
    }
    return vk::Filter::eLinear;
}

static vk::SamplerMipmapMode toVkMipmapMode(SamplerFilter f) {
    switch (f) {
    case SamplerFilter::Nearest: return vk::SamplerMipmapMode::eNearest;
    default:                     return vk::SamplerMipmapMode::eLinear;
    }
}

static vk::SamplerAddressMode toVkAddressMode(SamplerAddressMode m) {
    switch (m) {
    case SamplerAddressMode::Repeat:            return vk::SamplerAddressMode::eRepeat;
    case SamplerAddressMode::MirroredRepeat:    return vk::SamplerAddressMode::eMirroredRepeat;
    case SamplerAddressMode::ClampToEdge:       return vk::SamplerAddressMode::eClampToEdge;
    case SamplerAddressMode::ClampToBorder:     return vk::SamplerAddressMode::eClampToBorder;
    case SamplerAddressMode::MirrorClampToEdge: return vk::SamplerAddressMode::eMirrorClampToEdge;
    }
    return vk::SamplerAddressMode::eRepeat;
}

// ============================================================

VKSampler::VKSampler(const SamplerDesc& desc, vk::Device device)
    : m_desc(desc)
    , m_device(device)
{
    vk::SamplerCreateInfo ci;
    ci.magFilter    = toVkFilter(desc.magFilter);
    ci.minFilter    = toVkFilter(desc.minFilter);
    ci.mipmapMode   = toVkMipmapMode(desc.mipFilter);
    ci.addressModeU = toVkAddressMode(desc.addressU);
    ci.addressModeV = toVkAddressMode(desc.addressV);
    ci.addressModeW = toVkAddressMode(desc.addressW);
    ci.mipLodBias   = desc.mipLodBias;
    ci.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
    ci.maxAnisotropy    = desc.maxAniso;
    ci.compareEnable    = desc.compareEnable ? VK_TRUE : VK_FALSE;
    ci.compareOp        = toVkCompareOp(desc.compareFunc);
    ci.minLod           = desc.minLod;
    ci.maxLod           = desc.maxLod;
    ci.borderColor      = vk::BorderColor::eFloatTransparentBlack;
    ci.unnormalizedCoordinates = VK_FALSE;

    m_sampler = m_device.createSampler(ci);
}

VKSampler::~VKSampler() {
    if (m_sampler) {
        m_device.destroySampler(m_sampler);
        m_sampler = nullptr;
    }
}

} // namespace mulan::engine
