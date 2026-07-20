#include "detail/vk_swap_chain.h"
#include "detail/vk_texture.h"
#include "detail/vk_device.h"

#include "../rhi/engine_error_code.h"
#include <mulan/core/log/log.h>
#include <mulan/core/result/error.h>
#include <mulan/core/profiling/profile.h>
#include <algorithm>
#include <string>

namespace mulan::engine {

Result<std::unique_ptr<VKSwapChain>> VKSwapChain::create(const SwapChainDesc& desc, const InitParams& params) {
    MULAN_PROFILE_ZONE();

    auto obj = std::unique_ptr<VKSwapChain>(new VKSwapChain(desc, params));
    if (auto e = obj->createSwapChain(); e.code != 0)
        return std::unexpected(e);
    return obj;
}

VKSwapChain::~VKSwapChain() {
    try {
        params_.presentQueue.waitIdle();
    } catch (const vk::Error& error) {
        LOG_ERROR("[Vulkan] Present queue wait failed during swapchain destruction: {}", error.what());
    }
    cleanup();
    if (surface_) {
        params_.instance.destroySurfaceKHR(surface_);
        surface_ = nullptr;
    }
}

bool VKSwapChain::acquireNextImage(vk::Semaphore imageAvailable) {
    try {
        auto result = params_.device.acquireNextImageKHR(swapchain_, UINT64_MAX, imageAvailable, nullptr);
        if (result.result == vk::Result::eSuccess || result.result == vk::Result::eSuboptimalKHR) {
            current_image_index_ = result.value;
            return true;
        }
        LOG_WARN("[Vulkan] Swapchain image acquisition failed: result={}", vk::to_string(result.result));
    } catch (const vk::Error& error) {
        LOG_ERROR("[Vulkan] Swapchain image acquisition failed: {}", error.what());
    }
    return false;
}

ResultVoid VKSwapChain::presentWithSemaphores(vk::Semaphore renderFinished) {
    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &current_image_index_;
    try {
        const vk::Result result = params_.presentQueue.presentKHR(&presentInfo);
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
            return std::unexpected(
                    makeError(EngineErrorCode::PresentationFailed, "Vulkan swapchain presentation failed"));
    } catch (const vk::Error& error) {
        return std::unexpected(makeError(EngineErrorCode::PresentationFailed, error.what()));
    }
    return {};
}

ResultVoid VKSwapChain::present() {
    vk::PresentInfoKHR presentInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &current_image_index_;
    try {
        const vk::Result result = params_.presentQueue.presentKHR(&presentInfo);
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
            return std::unexpected(
                    makeError(EngineErrorCode::PresentationFailed, "Vulkan swapchain presentation failed"));
    } catch (const vk::Error& error) {
        return std::unexpected(makeError(EngineErrorCode::PresentationFailed, error.what()));
    }
    return {};
}

ResultVoid VKSwapChain::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "Vulkan swapchain size must be non-zero"));
    // Graphics submission 已由调用方精确等待；这里只等待 presentation queue
    // 释放旧 swapchain images，避免阻塞设备上的其他队列。
    try {
        params_.presentQueue.waitIdle();
    } catch (const vk::Error& error) {
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, error.what()));
    }
    cleanup();
    desc_.width = width;
    desc_.height = height;
    if (auto error = createSwapChain(); error.code != 0)
        return std::unexpected(std::move(error));
    return {};
}

Error VKSwapChain::createMsaaResources() {
    msaa_color_texture_.reset();
    if (desc_.sampleCount <= 1)
        return {};

    TextureDesc colorDesc =
            TextureDesc::renderTarget(swapchain_extent_.width, swapchain_extent_.height,
                                      fromVkFormat(swapchain_format_), "SwapchainMSAAColor", desc_.sampleCount);
    colorDesc.usage = TextureUsageFlags::RenderTarget;
    auto colorResult = VKTexture::create(colorDesc, params_.device, params_.allocator);
    if (!colorResult) {
        return colorResult.error();
    }
    msaa_color_texture_ = std::move(*colorResult);
    return {};
}

// --------------------------------------------------------
// SwapChain 创建
// --------------------------------------------------------

Error VKSwapChain::createSwapChain() {
    std::vector<vk::SurfaceFormatKHR> formats;
    try {
        auto caps = params_.physicalDevice.getSurfaceCapabilitiesKHR(surface_);
        formats = params_.physicalDevice.getSurfaceFormatsKHR(surface_);
        auto modes = params_.physicalDevice.getSurfacePresentModesKHR(surface_);

        if (formats.empty()) {
            return makeError(EngineErrorCode::SurfaceNotSupported, "getSurfaceFormatsKHR returned no formats");
        }

        vk::SurfaceFormatKHR surfaceFormat = formats[0];
        for (auto& f : formats) {
            // 优先选 B8G8R8A8Unorm + SrgbNonlinear 色彩空间（clear 值直写，不做硬件 gamma）
            if (f.format == vk::Format::eB8G8R8A8Unorm && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
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
            extent.width = (std::min) (std::max(desc_.width, caps.minImageExtent.width), caps.maxImageExtent.width);
            extent.height = (std::min) (std::max(desc_.height, caps.minImageExtent.height), caps.maxImageExtent.height);
        }

        // Vulkan 要求请求数量至少为 surface 的 minImageCount；maxImageCount 为 0 表示不设上限。
        uint32_t imageCount = (std::max) (desc_.bufferCount, caps.minImageCount);
        if (caps.maxImageCount > 0) {
            imageCount = (std::min) (imageCount, caps.maxImageCount);
        }

        uint32_t queueFamilyIndices[] = { params_.graphicsQueueFamily, params_.presentQueueFamily };

        vk::SwapchainCreateInfoKHR ci;
        ci.surface = surface_;
        ci.minImageCount = imageCount;
        ci.imageFormat = surfaceFormat.format;
        ci.imageColorSpace = surfaceFormat.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        ci.presentMode = presentMode;
        ci.clipped = true;

        if (params_.graphicsQueueFamily != params_.presentQueueFamily) {
            ci.imageSharingMode = vk::SharingMode::eConcurrent;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            ci.imageSharingMode = vk::SharingMode::eExclusive;
        }

        swapchain_ = params_.device.createSwapchainKHR(ci);

        swapchain_images_ = params_.device.getSwapchainImagesKHR(swapchain_);
        render_finished_semaphores_.reserve(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); ++i)
            render_finished_semaphores_.push_back(params_.device.createSemaphore({}));
        swapchain_format_ = surfaceFormat.format;
        swapchain_extent_ = extent;
        desc_.format = fromVkFormat(swapchain_format_);
        desc_.width = extent.width;
        desc_.height = extent.height;
        desc_.sampleCount = desc_.sampleCount > 1 ? desc_.sampleCount : 1;

        // 创建 ImageViews + VKTexture wrappers for backbuffers
        image_views_.resize(swapchain_images_.size());
        back_buffers_.clear();
        back_buffers_.reserve(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); ++i) {
            vk::ImageViewCreateInfo viewCI;
            viewCI.image = swapchain_images_[i];
            viewCI.viewType = vk::ImageViewType::e2D;
            viewCI.format = swapchain_format_;
            viewCI.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewCI.subresourceRange.levelCount = 1;
            viewCI.subresourceRange.layerCount = 1;
            image_views_[i] = params_.device.createImageView(viewCI);

            // 创建 VKTexture 包装（不拥有 image/view，生命周期由 swapchain 管理）
            auto backBufDesc = TextureDesc::renderTarget(extent.width, extent.height, fromVkFormat(swapchain_format_),
                                                         "SwapchainBackBuffer");

            back_buffers_.push_back(
                    std::make_unique<VKTexture>(backBufDesc, params_.device, swapchain_images_[i], image_views_[i]));
        }
    } catch (const vk::Error& e) {
        return makeError(EngineErrorCode::SwapChainCreateFailed, std::string("VKSwapChain create failed: ") + e.what());
    }

    // 深度缓冲（用 VKTexture::create）
    if (auto e = createMsaaResources(); e.code != 0) {
        return e;
    }

    if (desc_.hasDepth) {
        TextureDesc depthDesc = TextureDesc::depthStencil(swapchain_extent_.width, swapchain_extent_.height,
                                                          desc_.depthFormat, "DepthBuffer", desc_.sampleCount);
        auto depthResult = VKTexture::create(depthDesc, params_.device, params_.allocator);
        if (!depthResult) {
            return depthResult.error();
        }
        depth_texture_ = std::move(*depthResult);
    } else {
        depth_texture_.reset();
    }

    return {};
}

void VKSwapChain::cleanup() {
    for (vk::Semaphore semaphore : render_finished_semaphores_) {
        if (semaphore)
            params_.device.destroySemaphore(semaphore);
    }
    render_finished_semaphores_.clear();

    for (auto& iv : image_views_)
        params_.device.destroyImageView(iv);
    image_views_.clear();

    back_buffers_.clear();
    msaa_color_texture_.reset();
    depth_texture_.reset();

    if (swapchain_) {
        params_.device.destroySwapchainKHR(swapchain_);
        swapchain_ = nullptr;
    }
}

RenderPassBeginInfo VKSwapChain::renderPassBeginInfo() {
    RenderPassBeginInfo info;
    auto* backBuffer = currentBackBuffer();
    auto* color = msaa_color_texture_ ? static_cast<Texture*>(msaa_color_texture_.get()) : backBuffer;
    if (color) {
        info.colorAttachments[0].target = color;
        info.colorAttachments[0].resolveTarget = msaa_color_texture_ ? backBuffer : nullptr;
        info.colorAttachments[0].loadAction = LoadAction::Clear;
        info.colorAttachments[0].storeAction = msaa_color_texture_ ? StoreAction::DontCare : StoreAction::Store;
        info.colorCount = 1;
    }
    auto* depth = depthTexture();
    if (depth) {
        info.depthAttachment.target = depth;
        info.depthAttachment.loadAction = LoadAction::Clear;
        info.depthAttachment.storeAction = StoreAction::DontCare;
    }
    auto& cc = desc().clearColor;
    info.clearColor[0] = cc[0];
    info.clearColor[1] = cc[1];
    info.clearColor[2] = cc[2];
    info.clearColor[3] = cc[3];
    info.clearDepth = desc().clearDepth;
    info.presentSource = true;
    info.width = desc().width;
    info.height = desc().height;
    return info;
}

}  // namespace mulan::engine
