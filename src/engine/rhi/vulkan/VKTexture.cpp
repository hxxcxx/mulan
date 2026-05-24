#include "VKTexture.h"

namespace mulan::engine {

bool VKTexture::isDepthFormat(TextureFormat f) {
    return f == TextureFormat::D16_UNorm
        || f == TextureFormat::D24_UNorm_S8_UInt
        || f == TextureFormat::D32_Float
        || f == TextureFormat::D32_Float_S8X24_UInt;
}

VKTexture::VKTexture(const TextureDesc& desc, vk::Device device, VmaAllocator allocator)
    : m_desc(desc), m_device(device), m_allocator(allocator)
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
    vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&ci),
                   &allocCI, &image, &allocation, &allocInfo);

    m_image      = vk::Image(image);
    m_allocation = allocation;

    // 创建 ImageView
    vk::ImageViewCreateInfo viewCI;
    viewCI.image            = m_image;
    viewCI.viewType         = vk::ImageViewType::e2D;
    viewCI.format           = ci.format;
    viewCI.subresourceRange.aspectMask = isDepthFormat(desc.format)
        ? vk::ImageAspectFlagBits::eDepth
        : vk::ImageAspectFlagBits::eColor;
    viewCI.subresourceRange.levelCount     = desc.mipLevels;
    viewCI.subresourceRange.layerCount     = desc.arraySize;

    m_view = m_device.createImageView(viewCI);
}

/// Swapchain backbuffer wrapper (does NOT own image/view)
VKTexture::VKTexture(const TextureDesc& desc, vk::Device device, vk::Image existingImage, vk::ImageView existingView)
    : m_desc(desc), m_device(device), m_image(existingImage), m_view(existingView), m_ownsResources(false)
{
    m_currentLayout = vk::ImageLayout::eUndefined;
}

VKTexture::~VKTexture() {
    if (m_ownsResources) {
        if (m_view)      m_device.destroyImageView(m_view);
        if (m_image && m_allocator)
            vmaDestroyImage(m_allocator, VkImage(m_image), m_allocation);
    }
}

vk::ImageViewCreateInfo VKTexture::viewForFramebuffer() const {
    vk::ImageViewCreateInfo ci;
    ci.image            = m_image;
    ci.viewType         = vk::ImageViewType::e2D;
    ci.format           = toVkFormat(m_desc.format);
    ci.subresourceRange.aspectMask = isDepthFormat(m_desc.format)
        ? vk::ImageAspectFlagBits::eDepth
        : vk::ImageAspectFlagBits::eColor;
    ci.subresourceRange.levelCount = m_desc.mipLevels;
    ci.subresourceRange.layerCount = m_desc.arraySize;
    return ci;
}

} // namespace mulan::Engine
