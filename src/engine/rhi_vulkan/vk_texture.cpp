#include "detail/vk_texture.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <string>

namespace mulan::engine {

bool VKTexture::isDepthFormat(TextureFormat f) {
    return f == TextureFormat::D16_UNorm || f == TextureFormat::D24_UNorm_S8_UInt || f == TextureFormat::D32_Float ||
           f == TextureFormat::D32_Float_S8X24_UInt;
}

core::Result<std::unique_ptr<VKTexture>> VKTexture::create(const TextureDesc& desc, vk::Device device,
                                                           VmaAllocator allocator) {
    // 创建 Image
    vk::ImageCreateInfo ci;
    ci.imageType = desc.dimension == TextureDimension::Texture3D ? vk::ImageType::e3D : vk::ImageType::e2D;
    ci.format = toVkFormat(desc.format);
    ci.extent = vk::Extent3D(desc.width, desc.height, desc.depth);
    ci.mipLevels = desc.mipLevels;
    ci.arrayLayers = desc.arraySize;
    ci.samples = toVkSampleCount(desc.sampleCount);
    ci.tiling = vk::ImageTiling::eOptimal;
    ci.initialLayout = vk::ImageLayout::eUndefined;

    ci.usage = vk::ImageUsageFlagBits::eSampled;
    if (desc.usage & TextureUsageFlags::RenderTarget)
        ci.usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (desc.usage & TextureUsageFlags::DepthStencil)
        ci.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (desc.usage & TextureUsageFlags::UnorderedAccess)
        ci.usage |= vk::ImageUsageFlagBits::eStorage;
    if (desc.usage & TextureUsageFlags::TransferDst)
        ci.usage |= vk::ImageUsageFlagBits::eTransferDst;
    if (desc.usage & TextureUsageFlags::TransferSrc)
        ci.usage |= vk::ImageUsageFlagBits::eTransferSrc;
    if (desc.usage & TextureUsageFlags::GenerateMips)
        ci.usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage image;
    VmaAllocation allocation;
    VmaAllocationInfo allocInfo;
    VkResult res = vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&ci), &allocCI, &image,
                                  &allocation, &allocInfo);
    if (res != VK_SUCCESS) {
        return std::unexpected(makeError(EngineErrorCode::TextureCreateFailed,
                                         "vmaCreateImage failed: VkResult=" + std::to_string(res)));
    }

    vk::Image vkImage(image);

    // 创建 ImageView
    vk::ImageViewCreateInfo viewCI;
    viewCI.image = vkImage;
    viewCI.viewType = vk::ImageViewType::e2D;
    viewCI.format = ci.format;
    viewCI.subresourceRange.aspectMask =
            isDepthFormat(desc.format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    viewCI.subresourceRange.levelCount = desc.mipLevels;
    viewCI.subresourceRange.layerCount = desc.arraySize;

    vk::ImageView view;
    try {
        view = device.createImageView(viewCI);
    } catch (const vk::Error& e) {
        vmaDestroyImage(allocator, image, allocation);
        return std::unexpected(
                makeError(EngineErrorCode::TextureCreateFailed, std::string("createImageView failed: ") + e.what()));
    }

    return std::unique_ptr<VKTexture>(new VKTexture(desc, device, allocator, vkImage, allocation, view));
}

/// Swapchain backbuffer wrapper (does NOT own image/view)
VKTexture::VKTexture(const TextureDesc& desc, vk::Device device, vk::Image existingImage, vk::ImageView existingView)
    : desc_(desc), device_(device), image_(existingImage), view_(existingView), owns_resources_(false) {
    current_layout_ = vk::ImageLayout::eUndefined;
}

VKTexture::~VKTexture() {
    waitForLastUseBeforeDestruction();
    if (owns_resources_) {
        if (view_)
            device_.destroyImageView(view_);
        if (image_ && allocator_)
            vmaDestroyImage(allocator_, VkImage(image_), allocation_);
    }
}

vk::ImageViewCreateInfo VKTexture::viewForFramebuffer() const {
    vk::ImageViewCreateInfo ci;
    ci.image = image_;
    ci.viewType = vk::ImageViewType::e2D;
    ci.format = toVkFormat(desc_.format);
    ci.subresourceRange.aspectMask =
            isDepthFormat(desc_.format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    ci.subresourceRange.levelCount = desc_.mipLevels;
    ci.subresourceRange.layerCount = desc_.arraySize;
    return ci;
}

}  // namespace mulan::engine
