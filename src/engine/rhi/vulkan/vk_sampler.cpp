#include "vk_sampler.h"
#include "vk_convert.h"

#include <mulan/core/result/error.h>
#include "../engine_error_code.h"

#include <cstdio>
#include <string>

namespace mulan::engine {

// ============================================================
// Helper: RHI enum → Vulkan enum
// ============================================================

static vk::Filter toVkFilter(SamplerFilter f) {
    switch (f) {
    case SamplerFilter::Nearest: return vk::Filter::eNearest;
    case SamplerFilter::Linear: return vk::Filter::eLinear;
    case SamplerFilter::Anisotropic: return vk::Filter::eLinear;  // aniso 用 anisotropyEnable 控制
    }
    return vk::Filter::eLinear;
}

static vk::SamplerMipmapMode toVkMipmapMode(SamplerFilter f) {
    switch (f) {
    case SamplerFilter::Nearest: return vk::SamplerMipmapMode::eNearest;
    default: return vk::SamplerMipmapMode::eLinear;
    }
}

static vk::SamplerAddressMode toVkAddressMode(SamplerAddressMode m) {
    switch (m) {
    case SamplerAddressMode::Repeat: return vk::SamplerAddressMode::eRepeat;
    case SamplerAddressMode::MirroredRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
    case SamplerAddressMode::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
    case SamplerAddressMode::ClampToBorder: return vk::SamplerAddressMode::eClampToBorder;
    case SamplerAddressMode::MirrorClampToEdge: return vk::SamplerAddressMode::eMirrorClampToEdge;
    }
    return vk::SamplerAddressMode::eRepeat;
}

// ============================================================

core::Result<std::unique_ptr<VKSampler>> VKSampler::create(const SamplerDesc& desc, vk::Device device) {
    auto obj = std::unique_ptr<VKSampler>(new VKSampler(desc, device));

    vk::SamplerCreateInfo ci;
    ci.magFilter = toVkFilter(desc.magFilter);
    ci.minFilter = toVkFilter(desc.minFilter);
    ci.mipmapMode = toVkMipmapMode(desc.mipFilter);
    ci.addressModeU = toVkAddressMode(desc.addressU);
    ci.addressModeV = toVkAddressMode(desc.addressV);
    ci.addressModeW = toVkAddressMode(desc.addressW);
    ci.mipLodBias = desc.mipLodBias;
    ci.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
    ci.maxAnisotropy = desc.maxAniso;
    ci.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    ci.compareOp = toVkCompareOp(desc.compareFunc);
    ci.minLod = desc.minLod;
    ci.maxLod = desc.maxLod;
    ci.borderColor = vk::BorderColor::eFloatTransparentBlack;
    ci.unnormalizedCoordinates = VK_FALSE;

    try {
        obj->sampler_ = device.createSampler(ci);
    } catch (const vk::Error& e) {
        return std::unexpected(
                makeError(EngineErrorCode::SamplerCreateFailed, std::string("createSampler failed: ") + e.what()));
    }

    return obj;
}

VKSampler::~VKSampler() {
    if (sampler_) {
        device_.destroySampler(sampler_);
        sampler_ = nullptr;
    }
}

}  // namespace mulan::engine
