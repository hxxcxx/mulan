/**
 * @file vk_texture.h
 * @brief Vulkan纹理实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../texture.h"
#include "vk_convert.h"

namespace mulan::engine {

class VKTexture : public Texture {
public:
    /// Regular texture (owns resources)
    VKTexture(const TextureDesc& desc, vk::Device device, VmaAllocator allocator);

    /// Swapchain backbuffer wrapper (does NOT own image/view, fromSwapchain=true)
    VKTexture(const TextureDesc& desc, vk::Device device, vk::Image existingImage, vk::ImageView existingView);

    ~VKTexture();

    const TextureDesc& desc() const override { return desc_; }

    vk::Image image() const { return image_; }
    vk::ImageView view() const { return view_; }
    vk::ImageViewCreateInfo viewForFramebuffer() const;

    vk::ImageLayout currentLayout() const { return current_layout_; }
    void setCurrentLayout(vk::ImageLayout layout) { current_layout_ = layout; }

    static bool isDepthFormat(TextureFormat f);

private:
    TextureDesc     desc_;
    vk::Device      device_;
    VmaAllocator    allocator_ = nullptr;
    vk::Image       image_;
    VmaAllocation   allocation_ = nullptr;
    vk::ImageView   view_;
    vk::ImageLayout current_layout_ = vk::ImageLayout::eUndefined;
    bool            owns_resources_ = true;
};

} // namespace mulan::engine
