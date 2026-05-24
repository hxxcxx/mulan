/**
 * @file VKTexture.h
 * @brief Vulkan纹理实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../Texture.h"
#include "VkConvert.h"

namespace mulan::engine {

class VKTexture : public Texture {
public:
    /// Regular texture (owns resources)
    VKTexture(const TextureDesc& desc, vk::Device device, VmaAllocator allocator);

    /// Swapchain backbuffer wrapper (does NOT own image/view, fromSwapchain=true)
    VKTexture(const TextureDesc& desc, vk::Device device, vk::Image existingImage, vk::ImageView existingView);

    ~VKTexture();

    const TextureDesc& desc() const override { return m_desc; }

    vk::Image image() const { return m_image; }
    vk::ImageView view() const { return m_view; }
    vk::ImageViewCreateInfo viewForFramebuffer() const;

    vk::ImageLayout currentLayout() const { return m_currentLayout; }
    void setCurrentLayout(vk::ImageLayout layout) { m_currentLayout = layout; }

    static bool isDepthFormat(TextureFormat f);

private:
    TextureDesc     m_desc;
    vk::Device      m_device;
    VmaAllocator    m_allocator = nullptr;
    vk::Image       m_image;
    VmaAllocation   m_allocation = nullptr;
    vk::ImageView   m_view;
    vk::ImageLayout m_currentLayout = vk::ImageLayout::eUndefined;
    bool            m_ownsResources = true;
};

} // namespace mulan::Engine
