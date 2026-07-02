/**
 * @file vk_texture.h
 * @brief Vulkan纹理实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../texture.h"
#include "vk_convert.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class VKTexture : public Texture {
public:
    /// 创建常规纹理（拥有 image/view 资源）。失败返回 TextureCreateFailed。
    static std::expected<std::unique_ptr<VKTexture>, core::Error>
        create(const TextureDesc& desc, vk::Device device, VmaAllocator allocator);

    /// Swapchain backbuffer 包装构造（不拥有 image/view，仅持有句柄）。
    /// 不可失败，保持 public。
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
    // 主构造：仅 create() 使用
    VKTexture(const TextureDesc& desc, vk::Device device, VmaAllocator allocator,
              vk::Image image, VmaAllocation allocation, vk::ImageView view)
        : desc_(desc), device_(device), allocator_(allocator),
          image_(image), allocation_(allocation), view_(view) {}

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
