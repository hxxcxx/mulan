/**
 * @file vk_swap_chain.h
 * @brief Vulkan交换链实现，管理图像与帧缓冲
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../swap_chain.h"
#include "../../window.h"
#include "vk_convert.h"
#include "vk_command_list.h"
#include "vk_texture.h"

#include <array>

namespace mulan::engine {

class VKDevice;

class VKSwapChain : public SwapChain {
public:
    struct InitParams {
        vk::Instance       instance;
        vk::PhysicalDevice physicalDevice;
        vk::Device         device;
        VmaAllocator       allocator;
        uint32_t           graphicsQueueFamily;
        uint32_t           presentQueueFamily;
        vk::Queue          graphicsQueue;
        vk::Queue          presentQueue;
        vk::SurfaceKHR     surface;
        VKDevice*          ownerDevice = nullptr;  // 用于访问 RenderPass Cache
    };

    VKSwapChain(const SwapChainDesc& desc, const InitParams& params,
                const RenderConfig& renderConfig);
    ~VKSwapChain();

    const SwapChainDesc& desc() const override { return desc_; }

    Texture* currentBackBuffer() override {
        if (current_image_index_ < back_buffers_.size())
            return back_buffers_[current_image_index_].get();
        return nullptr;
    }

    Texture* depthTexture() override {
        return depth_texture_ ? depth_texture_.get() : nullptr;
    }

    bool acquireNextImage(vk::Semaphore imageAvailable);
    void presentWithSemaphores(vk::Semaphore renderFinished);
    void present() override;

    void resize(uint32_t width, uint32_t height) override;

    // --- Vulkan 特有 ---
    uint32_t currentImageIndex()  const { return current_image_index_; }
    uint32_t imageCount()         const { return static_cast<uint32_t>(swapchain_images_.size()); }
    vk::Extent2D extent()        const { return swapchain_extent_; }

private:
    void createSwapChain();
    void cleanup();

    SwapChainDesc    desc_;
    InitParams       params_;

    vk::SurfaceKHR   surface_;
    vk::SwapchainKHR swapchain_;
    std::vector<vk::Image>     swapchain_images_;
    std::vector<vk::ImageView> image_views_;
    vk::Format       swapchain_format_;
    vk::Extent2D     swapchain_extent_;

    std::unique_ptr<VKTexture> depth_texture_;
    std::vector<std::unique_ptr<VKTexture>> back_buffers_;

    uint32_t current_image_index_ = 0;
    RenderConfig render_config_;
};

} // namespace mulan::engine
