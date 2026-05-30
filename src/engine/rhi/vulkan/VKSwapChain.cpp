#include "VKSwapChain.h"
#include "VKTexture.h"
#include "VKDevice.h"
#include <algorithm>

namespace mulan::engine {

VKSwapChain::VKSwapChain(const SwapChainDesc& desc, const InitParams& params,
                         const RenderConfig& renderConfig)
    : m_desc(desc), m_params(params), m_surface(params.surface)
    , m_renderConfig(renderConfig)
{
    createSwapChain();
}

VKSwapChain::~VKSwapChain() {
    cleanup();
}

bool VKSwapChain::acquireNextImage(vk::Semaphore imageAvailable) {
    auto result = m_params.device.acquireNextImageKHR(
        m_swapchain, UINT64_MAX, imageAvailable, nullptr);
    if (result.result == vk::Result::eSuccess ||
        result.result == vk::Result::eSuboptimalKHR) {
        m_currentImageIndex = result.value;
        return true;
    }
    return false;
}

void VKSwapChain::presentWithSemaphores(vk::Semaphore renderFinished) {
    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapchain;
    presentInfo.pImageIndices      = &m_currentImageIndex;
    m_params.presentQueue.presentKHR(&presentInfo);
}

void VKSwapChain::present() {
    vk::PresentInfoKHR presentInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains    = &m_swapchain;
    presentInfo.pImageIndices  = &m_currentImageIndex;
    m_params.presentQueue.presentKHR(&presentInfo);
}

void VKSwapChain::resize(uint32_t width, uint32_t height) {
    m_params.device.waitIdle();
    if (m_params.ownerDevice) {
        m_params.ownerDevice->clearFramebufferCache();
    }
    cleanup();
    m_desc.width  = width;
    m_desc.height = height;
    createSwapChain();
}

// --------------------------------------------------------
// SwapChain 创建
// --------------------------------------------------------

void VKSwapChain::createSwapChain() {
    auto caps     = m_params.physicalDevice.getSurfaceCapabilitiesKHR(m_surface);
    auto formats  = m_params.physicalDevice.getSurfaceFormatsKHR(m_surface);
    auto modes    = m_params.physicalDevice.getSurfacePresentModesKHR(m_surface);

    vk::SurfaceFormatKHR surfaceFormat = formats[0];
    for (auto& f : formats) {
        // 优先选 B8G8R8A8Unorm + SrgbNonlinear 色彩空间（clear 值直写，不做硬件 gamma）
        if (f.format == vk::Format::eB8G8R8A8Unorm &&
            f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            surfaceFormat = f;
            break;
        }
    }

    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
    if (!m_desc.vsync) {
        for (auto& m : modes) {
            if (m == vk::PresentModeKHR::eMailbox) {
                presentMode = m;
                break;
            }
            if (m == vk::PresentModeKHR::eImmediate) {
                presentMode = m;
            }
        }
    }

    vk::Extent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width  = (std::min)(std::max(m_desc.width,
            caps.minImageExtent.width), caps.maxImageExtent.width);
        extent.height = (std::min)(std::max(m_desc.height,
            caps.minImageExtent.height), caps.maxImageExtent.height);
    }

    uint32_t imageCount = m_desc.bufferCount;
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    uint32_t queueFamilyIndices[] = {
        m_params.graphicsQueueFamily,
        m_params.presentQueueFamily
    };

    vk::SwapchainCreateInfoKHR ci;
    ci.surface          = m_surface;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = surfaceFormat.format;
    ci.imageColorSpace  = surfaceFormat.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = vk::ImageUsageFlagBits::eColorAttachment;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    ci.presentMode      = presentMode;
    ci.clipped          = true;

    if (m_params.graphicsQueueFamily != m_params.presentQueueFamily) {
        ci.imageSharingMode      = vk::SharingMode::eConcurrent;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = queueFamilyIndices;
    } else {
        ci.imageSharingMode = vk::SharingMode::eExclusive;
    }

    m_swapchain = m_params.device.createSwapchainKHR(ci);

    m_swapchainImages  = m_params.device.getSwapchainImagesKHR(m_swapchain);
    m_swapchainFormat  = surfaceFormat.format;
    m_swapchainExtent  = extent;
    m_desc.format      = fromVkFormat(m_swapchainFormat);
    m_desc.width       = extent.width;
    m_desc.height      = extent.height;

    // 创建 ImageViews + VKTexture wrappers for backbuffers
    m_imageViews.resize(m_swapchainImages.size());
    m_backBuffers.clear();
    m_backBuffers.reserve(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
        vk::ImageViewCreateInfo viewCI;
        viewCI.image            = m_swapchainImages[i];
        viewCI.viewType         = vk::ImageViewType::e2D;
        viewCI.format           = m_swapchainFormat;
        viewCI.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
        viewCI.subresourceRange.levelCount     = 1;
        viewCI.subresourceRange.layerCount     = 1;
        m_imageViews[i] = m_params.device.createImageView(viewCI);

        // 创建 VKTexture 包装（不拥有 image/view，生命周期由 swapchain 管理）
        auto backBufDesc = TextureDesc::renderTarget(
            extent.width, extent.height,
            fromVkFormat(m_swapchainFormat), "SwapchainBackBuffer");

        m_backBuffers.push_back(std::make_unique<VKTexture>(
            backBufDesc, m_params.device,
            m_swapchainImages[i], m_imageViews[i]));
    }

    // 深度缓冲
    TextureDesc depthDesc = TextureDesc::depthStencil(extent.width, extent.height,
                                                       m_desc.depthFormat,
                                                       "DepthBuffer");
    m_depthTexture = std::make_unique<VKTexture>(depthDesc, m_params.device, m_params.allocator);
}

void VKSwapChain::cleanup() {
    for (auto& iv : m_imageViews)
        m_params.device.destroyImageView(iv);
    m_imageViews.clear();

    m_backBuffers.clear();
    m_depthTexture.reset();

    if (m_swapchain) {
        m_params.device.destroySwapchainKHR(m_swapchain);
        m_swapchain = nullptr;
    }
}

} // namespace mulan::engine
