#include "vk_swap_chain.h"
#include "vk_texture.h"
#include "vk_device.h"
#include <algorithm>

namespace mulan::engine {

VKSwapChain::VKSwapChain(const SwapChainDesc& desc, const InitParams& params,
                         const RenderConfig& renderConfig)
    : desc_(desc), params_(params), surface_(params.surface)
    , render_config_(renderConfig)
{
    createSwapChain();
}

VKSwapChain::~VKSwapChain() {
    cleanup();
}

bool VKSwapChain::acquireNextImage(vk::Semaphore imageAvailable) {
    auto result = params_.device.acquireNextImageKHR(
        swapchain_, UINT64_MAX, imageAvailable, nullptr);
    if (result.result == vk::Result::eSuccess ||
        result.result == vk::Result::eSuboptimalKHR) {
        current_image_index_ = result.value;
        return true;
    }
    return false;
}

void VKSwapChain::presentWithSemaphores(vk::Semaphore renderFinished) {
    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain_;
    presentInfo.pImageIndices      = &current_image_index_;
    params_.presentQueue.presentKHR(&presentInfo);
}

void VKSwapChain::present() {
    vk::PresentInfoKHR presentInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains    = &swapchain_;
    presentInfo.pImageIndices  = &current_image_index_;
    params_.presentQueue.presentKHR(&presentInfo);
}

void VKSwapChain::resize(uint32_t width, uint32_t height) {
    params_.device.waitIdle();
    cleanup();
    desc_.width  = width;
    desc_.height = height;
    createSwapChain();
}

// --------------------------------------------------------
// SwapChain 创建
// --------------------------------------------------------

void VKSwapChain::createSwapChain() {
    auto caps     = params_.physicalDevice.getSurfaceCapabilitiesKHR(surface_);
    auto formats  = params_.physicalDevice.getSurfaceFormatsKHR(surface_);
    auto modes    = params_.physicalDevice.getSurfacePresentModesKHR(surface_);

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
    if (!desc_.vsync) {
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
        extent.width  = (std::min)(std::max(desc_.width,
            caps.minImageExtent.width), caps.maxImageExtent.width);
        extent.height = (std::min)(std::max(desc_.height,
            caps.minImageExtent.height), caps.maxImageExtent.height);
    }

    uint32_t imageCount = desc_.bufferCount;
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    uint32_t queueFamilyIndices[] = {
        params_.graphicsQueueFamily,
        params_.presentQueueFamily
    };

    vk::SwapchainCreateInfoKHR ci;
    ci.surface          = surface_;
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

    if (params_.graphicsQueueFamily != params_.presentQueueFamily) {
        ci.imageSharingMode      = vk::SharingMode::eConcurrent;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = queueFamilyIndices;
    } else {
        ci.imageSharingMode = vk::SharingMode::eExclusive;
    }

    swapchain_ = params_.device.createSwapchainKHR(ci);

    swapchain_images_  = params_.device.getSwapchainImagesKHR(swapchain_);
    swapchain_format_  = surfaceFormat.format;
    swapchain_extent_  = extent;
    desc_.format      = fromVkFormat(swapchain_format_);
    desc_.width       = extent.width;
    desc_.height      = extent.height;

    // 创建 ImageViews + VKTexture wrappers for backbuffers
    image_views_.resize(swapchain_images_.size());
    back_buffers_.clear();
    back_buffers_.reserve(swapchain_images_.size());
    for (size_t i = 0; i < swapchain_images_.size(); ++i) {
        vk::ImageViewCreateInfo viewCI;
        viewCI.image            = swapchain_images_[i];
        viewCI.viewType         = vk::ImageViewType::e2D;
        viewCI.format           = swapchain_format_;
        viewCI.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
        viewCI.subresourceRange.levelCount     = 1;
        viewCI.subresourceRange.layerCount     = 1;
        image_views_[i] = params_.device.createImageView(viewCI);

        // 创建 VKTexture 包装（不拥有 image/view，生命周期由 swapchain 管理）
        auto backBufDesc = TextureDesc::renderTarget(
            extent.width, extent.height,
            fromVkFormat(swapchain_format_), "SwapchainBackBuffer");

        back_buffers_.push_back(std::make_unique<VKTexture>(
            backBufDesc, params_.device,
            swapchain_images_[i], image_views_[i]));
    }

    // 深度缓冲
    TextureDesc depthDesc = TextureDesc::depthStencil(extent.width, extent.height,
                                                       desc_.depthFormat,
                                                       "DepthBuffer");
    depth_texture_ = std::make_unique<VKTexture>(depthDesc, params_.device, params_.allocator);
}

void VKSwapChain::cleanup() {
    for (auto& iv : image_views_)
        params_.device.destroyImageView(iv);
    image_views_.clear();

    back_buffers_.clear();
    depth_texture_.reset();

    if (swapchain_) {
        params_.device.destroySwapchainKHR(swapchain_);
        swapchain_ = nullptr;
    }
}

} // namespace mulan::engine
