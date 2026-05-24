/**
 * @file VKSwapChain.h
 * @brief Vulkan交换链实现，管理图像与帧缓冲
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../SwapChain.h"
#include "../../Window.h"
#include "VkConvert.h"
#include "VKCommandList.h"
#include "VKTexture.h"

#include <array>

namespace MulanGeo::engine {

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

    const SwapChainDesc& desc() const override { return m_desc; }

    Texture* currentBackBuffer() override {
        if (m_currentImageIndex < m_backBuffers.size())
            return m_backBuffers[m_currentImageIndex].get();
        return nullptr;
    }

    Texture* depthTexture() override {
        return m_depthTexture ? m_depthTexture.get() : nullptr;
    }

    bool acquireNextImage(vk::Semaphore imageAvailable);
    void presentWithSemaphores(vk::Semaphore renderFinished);
    void present() override;

    void resize(uint32_t width, uint32_t height) override;

    // --- Vulkan 特有 ---
    uint32_t currentImageIndex()  const { return m_currentImageIndex; }
    uint32_t imageCount()         const { return static_cast<uint32_t>(m_swapchainImages.size()); }
    vk::Extent2D extent()        const { return m_swapchainExtent; }

private:
    void createSwapChain();
    void cleanup();

    SwapChainDesc    m_desc;
    InitParams       m_params;

    vk::SurfaceKHR   m_surface;
    vk::SwapchainKHR m_swapchain;
    std::vector<vk::Image>     m_swapchainImages;
    std::vector<vk::ImageView> m_imageViews;
    vk::Format       m_swapchainFormat;
    vk::Extent2D     m_swapchainExtent;

    std::unique_ptr<VKTexture> m_depthTexture;
    std::vector<std::unique_ptr<VKTexture>> m_backBuffers;

    uint32_t m_currentImageIndex = 0;
    RenderConfig m_renderConfig;
};

} // namespace MulanGeo::Engine
