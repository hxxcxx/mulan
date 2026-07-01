#include "vk_texture.h"

#include <stdexcept>

namespace mulan::engine {

bool VKTexture::isDepthFormat(TextureFormat f) {
    return f == TextureFormat::D16_UNorm
        || f == TextureFormat::D24_UNorm_S8_UInt
        || f == TextureFormat::D32_Float
        || f == TextureFormat::D32_Float_S8X24_UInt;
}

VKTexture::VKTexture(const TextureDesc& desc, vk::Device device, VmaAllocator allocator)
    : desc_(desc), device_(device), allocator_(allocator)
{
    // 创建 Image
    vk::ImageCreateInfo ci;
    ci.imageType     = desc.dimension == TextureDimension::Texture3D
                       ? vk::ImageType::e3D
                       : vk::ImageType::e2D;
    ci.format        = toVkFormat(desc.format);
    ci.extent        = vk::Extent3D(desc.width, desc.height, desc.depth);
    ci.mipLevels     = desc.mipLevels;
    ci.arrayLayers   = desc.arraySize;
    ci.samples       = vk::SampleCountFlagBits::e1;
    ci.tiling        = vk::ImageTiling::eOptimal;
    ci.initialLayout = vk::ImageLayout::eUndefined;

    ci.usage = vk::ImageUsageFlagBits::eSampled;
    if (desc.usage & TextureUsageFlags::RenderTarget)
        ci.usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (desc.usage & TextureUsageFlags::DepthStencil)
        ci.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (desc.usage & TextureUsageFlags::UnorderedAccess)
        ci.usage |= vk::ImageUsageFlagBits::eStorage;
    if (desc.usage & TextureUsageFlags::GenerateMips)
        ci.usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage image;
    VmaAllocation allocation;
    VmaAllocationInfo allocInfo;
    VkResult res = vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&ci),
                                  &allocCI, &image, &allocation, &allocInfo);
    if (res != VK_SUCCESS)
        throw std::runtime_error("vmaCreateImage failed: VkResult=" + std::to_string(res));

    image_      = vk::Image(image);
    allocation_ = allocation;

    // 创建 ImageView
    vk::ImageViewCreateInfo viewCI;
    viewCI.image            = image_;
    viewCI.viewType         = vk::ImageViewType::e2D;
    viewCI.format           = ci.format;
    viewCI.subresourceRange.aspectMask = isDepthFormat(desc.format)
        ? vk::ImageAspectFlagBits::eDepth
        : vk::ImageAspectFlagBits::eColor;
    viewCI.subresourceRange.levelCount     = desc.mipLevels;
    viewCI.subresourceRange.layerCount     = desc.arraySize;

    view_ = device_.createImageView(viewCI);
}

/// Swapchain backbuffer wrapper (does NOT own image/view)
VKTexture::VKTexture(const TextureDesc& desc, vk::Device device, vk::Image existingImage, vk::ImageView existingView)
    : desc_(desc), device_(device), image_(existingImage), view_(existingView), owns_resources_(false)
{
    current_layout_ = vk::ImageLayout::eUndefined;
}

VKTexture::~VKTexture() {
    if (owns_resources_) {
        if (view_)      device_.destroyImageView(view_);
        if (image_ && allocator_)
            vmaDestroyImage(allocator_, VkImage(image_), allocation_);
    }
}

vk::ImageViewCreateInfo VKTexture::viewForFramebuffer() const {
    vk::ImageViewCreateInfo ci;
    ci.image            = image_;
    ci.viewType         = vk::ImageViewType::e2D;
    ci.format           = toVkFormat(desc_.format);
    ci.subresourceRange.aspectMask = isDepthFormat(desc_.format)
        ? vk::ImageAspectFlagBits::eDepth
        : vk::ImageAspectFlagBits::eColor;
    ci.subresourceRange.levelCount = desc_.mipLevels;
    ci.subresourceRange.layerCount = desc_.arraySize;
    return ci;
}

} // namespace mulan::engine
