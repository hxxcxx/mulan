// VMA 实现（必须在 #include <vk_mem_alloc.h> 之前）
#define VMA_IMPLEMENTATION

#include "VKDevice.h"

// Vulkan-Hpp 动态 dispatch 存储（必须在 #include <vulkan/vulkan.hpp> 之后）
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <algorithm>

namespace mulan::engine {

// ============================================================
// 资源创建
// ============================================================

ResourcePtr<Buffer> VKDevice::createBuffer(const BufferDesc& desc) {
    auto* buf = new VKBuffer(desc, m_allocator);
    if (buf->needsUpload()) {
        m_uploadContext->uploadBufferInit(buf);
    }
    return ResourcePtr<Buffer>(buf, DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Texture> VKDevice::createTexture(const TextureDesc& desc) {
    return ResourcePtr<Texture>(new VKTexture(desc, m_device, m_allocator), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Shader> VKDevice::createShader(const ShaderDesc& desc) {
    return ResourcePtr<Shader>(new VKShader(desc, m_device), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<PipelineState> VKDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    return ResourcePtr<PipelineState>(new VKPipelineState(desc, m_device, this), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<CommandList> VKDevice::createCommandList() {
    // 独立命令列表需要独立的 descriptor allocator
    auto* allocator = new VKDescriptorAllocator(m_device);
    m_standaloneAllocators.emplace_back(allocator);
    auto* cmd = new VKCommandList(m_device, m_graphicsQueueFamily, allocator);
    return ResourcePtr<CommandList>(cmd, DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<SwapChain> VKDevice::createSwapChain(const SwapChainDesc& desc) {
    VKSwapChain::InitParams params;
    params.instance            = m_instance;
    params.physicalDevice      = m_physicalDevice;
    params.device              = m_device;
    params.allocator           = m_allocator;
    params.graphicsQueueFamily = m_graphicsQueueFamily;
    params.presentQueueFamily  = m_presentQueueFamily;
    params.graphicsQueue       = m_graphicsQueue;
    params.presentQueue        = m_presentQueue;
    params.surface             = m_surface;
    params.ownerDevice         = this;

    auto* swapchain = new VKSwapChain(desc, params, m_renderConfig);
    m_swapChains.push_back(swapchain);

    // 为每个 swapchain image 创建独立的 renderFinished 信号量
    for (uint32_t i = static_cast<uint32_t>(m_renderFinishedSemaphores.size());
         i < swapchain->imageCount(); ++i) {
        m_renderFinishedSemaphores.push_back(m_device.createSemaphore({}));
    }

    if (m_frameContexts.empty()) {
        initFrameContexts(m_frameCount);
    }

    return ResourcePtr<SwapChain>(swapchain, DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Fence> VKDevice::createFence(uint64_t initialValue) {
    return ResourcePtr<Fence>(new VKFence(m_device, initialValue), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<RenderTarget> VKDevice::createRenderTarget(const RenderTargetDesc& desc) {
    auto* rt = new VKRenderTarget(desc, m_device, m_allocator);

    // 如果 frame contexts 还未初始化（无 SwapChain 时），在此初始化
    if (m_frameContexts.empty()) {
        initFrameContexts(m_frameCount);
    }

    return ResourcePtr<RenderTarget>(rt, DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Sampler> VKDevice::createSampler(const SamplerDesc& desc) {
    return ResourcePtr<Sampler>(new VKSampler(desc, m_device), DeviceResourceDeleter{shared_from_this()});
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
    auto it = std::find(m_swapChains.begin(), m_swapChains.end(), resource);
    if (it != m_swapChains.end()) m_swapChains.erase(it);
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

        m_graphicsQueue.submit(submitInfo);
    } else {
        m_graphicsQueue.submit(submitInfo);
    }
}

void VKDevice::waitIdle() {
    m_device.waitIdle();
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

    auto it = m_renderPassCache.find(key);
    if (it != m_renderPassCache.end()) {
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

    vk::RenderPass renderPass = m_device.createRenderPass(rpCI);
    m_renderPassCache.emplace(key, renderPass);
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

    m_descriptorAllocators[m_currentFrame]->resetPools();

    // 回收上一轮独立 CommandList 的 descriptor allocator
    // 这些 allocator 对应的 CommandList 应已在上一帧完成使用
    m_standaloneAllocators.clear();

    m_frameCmdList = std::make_unique<VKCommandList>(m_device, frame.cmdBuffer(),
                                                    m_descriptorAllocators[m_currentFrame].get());
    m_frameCmdList->setOwnerDevice(this);

    if (!m_swapChains.empty()) {
        auto* sc = m_swapChains[0];
        sc->acquireNextImage(frame.imageAvailable());
        m_acquiredImageIndex = sc->currentImageIndex();
    }
}

CommandList* VKDevice::frameCommandList() {
    return m_frameCmdList.get();
}

void VKDevice::submitAndPresent(SwapChain* swapchain) {
    auto* vkSC  = static_cast<VKSwapChain*>(swapchain);
    auto& frame = currentFrameContext();

    // 使用 per-image 的 renderFinished 信号量
    vk::Semaphore renderFinished = m_renderFinishedSemaphores[m_acquiredImageIndex];

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = static_cast<VKCommandList*>(m_frameCmdList.get())->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    vk::Semaphore waitSemaphores[] = { frame.imageAvailable() };
    vk::PipelineStageFlags waitStages[] = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput
    };
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;

    submitInfo.signalSemaphoreCount  = 1;
    submitInfo.pSignalSemaphores     = &renderFinished;

    m_graphicsQueue.submit(submitInfo, frame.inFlightFence());

    vkSC->presentWithSemaphores(renderFinished);

    m_currentFrame = (m_currentFrame + 1) % m_frameCount;
}

void VKDevice::submitOffscreen() {
    auto& frame = currentFrameContext();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = static_cast<VKCommandList*>(m_frameCmdList.get())->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    m_graphicsQueue.submit(submitInfo, frame.inFlightFence());

    m_currentFrame = (m_currentFrame + 1) % m_frameCount;
}

// ============================================================
// 帧上下文初始化
// ============================================================

void VKDevice::initFrameContexts(uint32_t count) {
    m_frameContexts.clear();
    m_frameCount = count;
    for (uint32_t i = 0; i < count; ++i) {
        m_frameContexts.push_back(
            std::make_unique<VKFrameContext>(m_device, m_graphicsQueueFamily));
    }

    // 同步 per-frame descriptor allocator 数量
    m_descriptorAllocators.clear();
    for (uint32_t i = 0; i < count; ++i) {
        m_descriptorAllocators.push_back(
            std::make_unique<VKDescriptorAllocator>(m_device));
    }

    m_currentFrame = 0;
    m_frameCmdList = std::make_unique<VKCommandList>(
        m_device, currentFrameContext().cmdBuffer(),
        m_descriptorAllocators[m_currentFrame].get());
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

    auto it = m_framebufferCache.find(key);
    if (it != m_framebufferCache.end()) {
        return it->second;
    }

    vk::FramebufferCreateInfo fbCI;
    fbCI.renderPass      = renderPass;
    fbCI.attachmentCount = key.attachmentCount;
    fbCI.pAttachments    = key.attachments.data();
    fbCI.width           = width;
    fbCI.height          = height;
    fbCI.layers          = 1;

    vk::Framebuffer fb = m_device.createFramebuffer(fbCI);
    m_framebufferCache.emplace(key, fb);
    return fb;
}

void VKDevice::clearFramebufferCache() {
    for (auto& [k, fb] : m_framebufferCache) {
        if (fb) m_device.destroyFramebuffer(fb);
    }
    m_framebufferCache.clear();
}

} // namespace mulan::Engine
