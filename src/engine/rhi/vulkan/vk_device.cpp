// VMA 实现（必须在 #include <vk_mem_alloc.h> 之前）
#define VMA_IMPLEMENTATION

#include "vk_device.h"

// Vulkan-Hpp 动态 dispatch 存储（必须在 #include <vulkan/vulkan.hpp> 之后）
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <algorithm>
#include <mulan/core/result/error.h>


namespace mulan::engine {

// ============================================================
// 资源创建
// ============================================================

ResourcePtr<Buffer> VKDevice::createBuffer(const BufferDesc& desc) {
    try {
        auto* buf = new VKBuffer(desc, allocator_);
        if (buf->needsUpload()) {
            upload_context_->uploadBufferInit(buf);
        }
        return ResourcePtr<Buffer>(buf, DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<Texture> VKDevice::createTexture(const TextureDesc& desc) {
    try {
        return ResourcePtr<Texture>(new VKTexture(desc, device_, allocator_),
                                    DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<Shader> VKDevice::createShader(const ShaderDesc& desc) {
    try {
        return ResourcePtr<Shader>(new VKShader(desc, device_),
                                   DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<PipelineState> VKDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    try {
        return ResourcePtr<PipelineState>(new VKPipelineState(desc, device_, this),
                                          DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<CommandList> VKDevice::createCommandList() {
    try {
        // 独立命令列表需要独立的 descriptor allocator
        auto* allocator = new VKDescriptorAllocator(device_);
        standalone_allocators_.emplace_back(allocator);
        auto* cmd = new VKCommandList(device_, graphics_queue_family_, allocator);
        return ResourcePtr<CommandList>(cmd, DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<SwapChain> VKDevice::createSwapChain(const SwapChainDesc& desc) {
    try {
        VKSwapChain::InitParams params;
        params.instance            = instance_;
        params.physicalDevice      = physical_device_;
        params.device              = device_;
        params.allocator           = allocator_;
        params.graphicsQueueFamily = graphics_queue_family_;
        params.presentQueueFamily  = present_queue_family_;
        params.graphicsQueue       = graphics_queue_;
        params.presentQueue        = present_queue_;
        params.surface             = surface_;
        params.ownerDevice         = this;

        auto* swapchain = new VKSwapChain(desc, params, render_config_);
        swap_chains_.push_back(swapchain);

        // 为每个 swapchain image 创建独立的 renderFinished 信号量
        for (uint32_t i = static_cast<uint32_t>(render_finished_semaphores_.size());
             i < swapchain->imageCount(); ++i) {
            render_finished_semaphores_.push_back(device_.createSemaphore({}));
        }

        if (frame_contexts_.empty()) {
            initFrameContexts(frame_count_);
        }

        return ResourcePtr<SwapChain>(swapchain, DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<Fence> VKDevice::createFence(uint64_t initialValue) {
    try {
        return ResourcePtr<Fence>(new VKFence(device_, initialValue),
                                  DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<RenderTarget> VKDevice::createRenderTarget(const RenderTargetDesc& desc) {
    try {
        auto* rt = new VKRenderTarget(desc, device_, allocator_);

        // 如果 frame contexts 还未初始化（无 SwapChain 时），在此初始化
        if (frame_contexts_.empty()) {
            initFrameContexts(frame_count_);
        }

        return ResourcePtr<RenderTarget>(rt, DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

ResourcePtr<Sampler> VKDevice::createSampler(const SamplerDesc& desc) {
    try {
        return ResourcePtr<Sampler>(new VKSampler(desc, device_),
                                    DeviceResourceDeleter{shared_from_this()});
    } catch (const std::exception& e) {
        return nullptr;
    }
}

// ============================================================
// 资源销毁
// ============================================================

void VKDevice::destroy(Buffer* resource)         { delete static_cast<VKBuffer*>(resource); }
void VKDevice::destroy(Texture* resource)        { delete static_cast<VKTexture*>(resource); }
void VKDevice::destroy(Shader* resource)         { delete static_cast<VKShader*>(resource); }
void VKDevice::destroy(PipelineState* resource)  { delete static_cast<VKPipelineState*>(resource); }
void VKDevice::destroy(CommandList* resource)    { delete static_cast<VKCommandList*>(resource); }
void VKDevice::destroy(Fence* resource)          { delete static_cast<VKFence*>(resource); }
void VKDevice::destroy(RenderTarget* resource)   { delete static_cast<VKRenderTarget*>(resource); }
void VKDevice::destroy(Sampler* resource)        { delete static_cast<VKSampler*>(resource); }

void VKDevice::destroy(SwapChain* resource) {
    auto it = std::find(swap_chains_.begin(), swap_chains_.end(), resource);
    if (it != swap_chains_.end()) swap_chains_.erase(it);
    delete static_cast<VKSwapChain*>(resource);
}

// ============================================================
// 提交命令
// ============================================================

void VKDevice::executeCommandLists(CommandList** cmdLists, uint32_t count,
                                   Fence* fence, uint64_t fenceValue) {
    std::vector<vk::CommandBuffer> cmdBuffers(count);
    for (uint32_t i = 0; i < count; ++i) {
        cmdBuffers[i] = static_cast<VKCommandList*>(cmdLists[i])->cmdBuffer();
    }

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = count;
    submitInfo.pCommandBuffers    = cmdBuffers.data();

    if (fence) {
        auto* vkFence = static_cast<VKFence*>(fence);
        vk::TimelineSemaphoreSubmitInfo timelineInfo;
        timelineInfo.signalSemaphoreValueCount = 1;
        uint64_t signalValue = fenceValue;
        timelineInfo.pSignalSemaphoreValues = &signalValue;

        vk::Semaphore semaphore = vkFence->semaphore();
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &semaphore;
        submitInfo.pNext                = &timelineInfo;

        graphics_queue_.submit(submitInfo);
    } else {
        graphics_queue_.submit(submitInfo);
    }
}

void VKDevice::waitIdle() {
    device_.waitIdle();
}

// ============================================================
// RenderPass Cache
// ============================================================

size_t VKDevice::RenderPassKeyHash::operator()(const RenderPassKey& k) const noexcept {
    size_t h = 0;
    // FNV-1a style hash
    auto combine = [&](auto val) {
        h ^= std::hash<decltype(val)>{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    for (uint8_t i = 0; i < k.colorCount; ++i) {
        combine(static_cast<uint8_t>(k.colorFormats[i]));
    }
    combine(k.colorCount);
    combine(static_cast<uint8_t>(k.depthFormat));
    combine(k.depthEnable);
    combine(static_cast<uint32_t>(k.colorLoadOp));
    combine(static_cast<uint32_t>(k.colorStoreOp));
    combine(static_cast<uint32_t>(k.colorFinalLayout));
    return h;
}

vk::RenderPass VKDevice::getOrCreateRenderPass(
    std::span<const TextureFormat> colorFormats,
    TextureFormat depthFormat,
    bool depthEnable,
    vk::AttachmentLoadOp colorLoadOp,
    vk::AttachmentStoreOp colorStoreOp,
    vk::ImageLayout colorFinalLayout)
{
    RenderPassKey key;
    key.colorCount = static_cast<uint8_t>(std::min(colorFormats.size(), key.colorFormats.size()));
    for (uint8_t i = 0; i < key.colorCount; ++i) {
        key.colorFormats[i] = colorFormats[i];
    }
    key.depthFormat      = depthFormat;
    key.depthEnable      = depthEnable;
    key.colorLoadOp      = colorLoadOp;
    key.colorStoreOp     = colorStoreOp;
    key.colorFinalLayout = colorFinalLayout;

    auto it = render_pass_cache_.find(key);
    if (it != render_pass_cache_.end()) {
        return it->second;
    }

    // --- 创建 vk::RenderPass ---
    std::vector<vk::AttachmentDescription> attachments;

    // Color attachments
    std::vector<vk::AttachmentReference> colorRefs;
    for (uint8_t i = 0; i < key.colorCount; ++i) {
        vk::AttachmentDescription colorAtt;
        colorAtt.format         = toVkFormat(key.colorFormats[i]);
        colorAtt.samples        = vk::SampleCountFlagBits::e1;
        colorAtt.loadOp         = key.colorLoadOp;
        colorAtt.storeOp        = key.colorStoreOp;
        colorAtt.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
        colorAtt.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAtt.initialLayout  = vk::ImageLayout::eUndefined;
        colorAtt.finalLayout    = key.colorFinalLayout;
        attachments.push_back(colorAtt);

        colorRefs.push_back({i, vk::ImageLayout::eColorAttachmentOptimal});
    }

    // Depth attachment
    vk::AttachmentReference depthRef;
    if (key.depthEnable) {
        vk::AttachmentDescription depthAtt;
        depthAtt.format         = toVkFormat(key.depthFormat);
        depthAtt.samples        = vk::SampleCountFlagBits::e1;
        depthAtt.loadOp         = vk::AttachmentLoadOp::eClear;
        depthAtt.storeOp        = vk::AttachmentStoreOp::eDontCare;
        depthAtt.stencilLoadOp  = vk::AttachmentLoadOp::eClear;
        depthAtt.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAtt.initialLayout  = vk::ImageLayout::eUndefined;
        depthAtt.finalLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        attachments.push_back(depthAtt);

        depthRef.attachment = key.colorCount;
        depthRef.layout     = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount    = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments       = colorRefs.data();
    subpass.pDepthStencilAttachment = key.depthEnable ? &depthRef : nullptr;

    // Subpass dependency: external → subpass 0
    // 确保之前的渲染操作（如 swapchain present）完成后才开始 color/depth 写入
    vk::SubpassDependency dependency;
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput
                             | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput
                             | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.srcAccessMask = {};
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite
                             | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::RenderPassCreateInfo rpCI;
    rpCI.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpCI.pAttachments    = attachments.data();
    rpCI.subpassCount    = 1;
    rpCI.pSubpasses      = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies   = &dependency;

    vk::RenderPass renderPass = device_.createRenderPass(rpCI);
    render_pass_cache_.emplace(key, renderPass);
    return renderPass;
}

// ============================================================
// 帧循环
// ============================================================

void VKDevice::beginFrame() {
    auto& frame = currentFrameContext();
    frame.waitForFence();
    frame.resetFence();

    frame.resetCommandBuffer();

    descriptor_allocators_[current_frame_]->resetPools();

    // 回收上一轮独立 CommandList 的 descriptor allocator
    // 这些 allocator 对应的 CommandList 应已在上一帧完成使用
    standalone_allocators_.clear();

    frame_cmd_list_ = std::make_unique<VKCommandList>(device_, frame.cmdBuffer(),
                                                    descriptor_allocators_[current_frame_].get());
    frame_cmd_list_->setOwnerDevice(this);

    if (!swap_chains_.empty()) {
        auto* sc = swap_chains_[0];
        sc->acquireNextImage(frame.imageAvailable());
        acquired_image_index_ = sc->currentImageIndex();
    }
}

CommandList* VKDevice::frameCommandList() {
    return frame_cmd_list_.get();
}

void VKDevice::submitAndPresent(SwapChain* swapchain) {
    submit();
    present(swapchain);
}

void VKDevice::submit() {
    auto& frame = currentFrameContext();

    // 使用 per-image 的 renderFinished 信号量
    pending_render_finished_ = render_finished_semaphores_[acquired_image_index_];

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = static_cast<VKCommandList*>(frame_cmd_list_.get())->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    vk::Semaphore waitSemaphores[] = { frame.imageAvailable() };
    vk::PipelineStageFlags waitStages[] = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput
    };
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;

    submitInfo.signalSemaphoreCount  = 1;
    submitInfo.pSignalSemaphores     = &pending_render_finished_;

    graphics_queue_.submit(submitInfo, frame.inFlightFence());
    submitted_ = true;
}

void VKDevice::present(SwapChain* swapchain) {
    auto* vkSC = static_cast<VKSwapChain*>(swapchain);
    if (submitted_ && pending_render_finished_) {
        vkSC->presentWithSemaphores(pending_render_finished_);
    } else {
        vkSC->present();
    }
    submitted_ = false;
    pending_render_finished_ = nullptr;
    current_frame_ = (current_frame_ + 1) % frame_count_;
}

void VKDevice::submitOffscreen() {
    auto& frame = currentFrameContext();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = static_cast<VKCommandList*>(frame_cmd_list_.get())->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    graphics_queue_.submit(submitInfo, frame.inFlightFence());

    current_frame_ = (current_frame_ + 1) % frame_count_;
}

// ============================================================
// 帧上下文初始化
// ============================================================

void VKDevice::initFrameContexts(uint32_t count) {
    frame_contexts_.clear();
    frame_count_ = count;
    for (uint32_t i = 0; i < count; ++i) {
        frame_contexts_.push_back(
            std::make_unique<VKFrameContext>(device_, graphics_queue_family_));
    }

    // 同步 per-frame descriptor allocator 数量
    descriptor_allocators_.clear();
    for (uint32_t i = 0; i < count; ++i) {
        descriptor_allocators_.push_back(
            std::make_unique<VKDescriptorAllocator>(device_));
    }

    current_frame_ = 0;
    frame_cmd_list_ = std::make_unique<VKCommandList>(
        device_, currentFrameContext().cmdBuffer(),
        descriptor_allocators_[current_frame_].get());
}

// ============================================================
// Framebuffer Cache
// ============================================================

size_t VKDevice::FramebufferKeyHash::operator()(const FramebufferKey& k) const noexcept {
    size_t h = 0;
    auto combine = [&](auto v) { h ^= std::hash<decltype(v)>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<VkRenderPass>(k.renderPass))));
    for (uint8_t i = 0; i < k.attachmentCount; ++i) {
        combine(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<VkImageView>(k.attachments[i]))));
    }
    combine(k.attachmentCount);
    combine(k.width);
    combine(k.height);
    return h;
}

vk::Framebuffer VKDevice::getOrCreateFramebuffer(
    vk::RenderPass renderPass,
    std::span<const vk::ImageView> attachments,
    uint32_t width, uint32_t height)
{
    FramebufferKey key;
    key.renderPass      = renderPass;
    key.attachmentCount = static_cast<uint8_t>(std::min(attachments.size(), key.attachments.size()));
    for (uint8_t i = 0; i < key.attachmentCount; ++i) {
        key.attachments[i] = attachments[i];
    }
    key.width  = width;
    key.height = height;

    auto it = framebuffer_cache_.find(key);
    if (it != framebuffer_cache_.end()) {
        return it->second;
    }

    vk::FramebufferCreateInfo fbCI;
    fbCI.renderPass      = renderPass;
    fbCI.attachmentCount = key.attachmentCount;
    fbCI.pAttachments    = key.attachments.data();
    fbCI.width           = width;
    fbCI.height          = height;
    fbCI.layers          = 1;

    vk::Framebuffer fb = device_.createFramebuffer(fbCI);
    framebuffer_cache_.emplace(key, fb);
    return fb;
}

void VKDevice::clearFramebufferCache() {
    for (auto& [k, fb] : framebuffer_cache_) {
        if (fb) device_.destroyFramebuffer(fb);
    }
    framebuffer_cache_.clear();
}

} // namespace mulan::engine
