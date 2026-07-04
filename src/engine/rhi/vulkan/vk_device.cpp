// VMA 实现（必须在 #include <vk_mem_alloc.h> 之前）
#define VMA_IMPLEMENTATION

#include "vk_device.h"

// Vulkan-Hpp 动态 dispatch 存储（必须在 #include <vulkan/vulkan.hpp> 之后）
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <algorithm>
#include <cstdio>
#include <string>
#include <mulan/core/result/error.h>
#include "../../engine_error_code.h"
#include "vk_debug_name.h"
#include "vk_bind_group.h"


namespace mulan::engine {

// ============================================================
// 资源创建
// ============================================================

std::expected<std::unique_ptr<Buffer>, core::Error> VKDevice::createBuffer(const BufferDesc& desc) {
    auto result = VKBuffer::create(desc, allocator_);
    if (!result) return std::unexpected(result.error());
    auto& buf = *result;
    setDebugName(device_, vk::ObjectType::eBuffer,
                 reinterpret_cast<uint64_t>(VkBuffer(buf->vkBuffer())),
                 desc.name.empty() ? "Buffer" : desc.name);
    if (buf->needsUpload()) {
        upload_context_->uploadBufferInit(buf.get());
    }
    return result;
}

std::expected<std::unique_ptr<Texture>, core::Error> VKDevice::createTexture(const TextureDesc& desc) {
    auto result = VKTexture::create(desc, device_, allocator_);
    if (!result) return std::unexpected(result.error());
    auto& tex = *result;
    setDebugName(device_, vk::ObjectType::eImage,
                 reinterpret_cast<uint64_t>(VkImage(tex->image())),
                 desc.name.empty() ? "Texture" : desc.name);
    setDebugName(device_, vk::ObjectType::eImageView,
                 reinterpret_cast<uint64_t>(VkImageView(tex->view())),
                 desc.name.empty() ? "TextureView" : (std::string(desc.name) + "/view").c_str());
    return result;
}

std::expected<std::unique_ptr<Shader>, core::Error> VKDevice::createShader(const ShaderDesc& desc) {
    auto result = VKShader::create(desc, device_);
    if (!result) return std::unexpected(result.error());
    auto& sh = *result;
    setDebugName(device_, vk::ObjectType::eShaderModule,
                 reinterpret_cast<uint64_t>(VkShaderModule(sh->module())),
                 desc.name.empty() ? "Shader" : desc.name);
    return result;
}

std::expected<std::unique_ptr<PipelineState>, core::Error>
VKDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    auto result = VKPipelineState::create(desc, device_, this);
    if (!result) return std::unexpected(result.error());
    auto& pso = *result;
    setDebugName(device_, vk::ObjectType::ePipeline,
                 reinterpret_cast<uint64_t>(VkPipeline(pso->pipeline())),
                 desc.name.empty() ? "Pipeline" : desc.name);
    return result;
}

std::expected<std::unique_ptr<CommandList>, core::Error> VKDevice::createCommandList() {
    // 独立命令列表需要独立的 descriptor allocator
    auto* allocator = new VKDescriptorAllocator(device_);
    auto result = VKCommandList::create(device_, graphics_queue_family_, allocator);
    if (!result) {
        delete allocator;
        return std::unexpected(result.error());
    }
    standalone_allocators_.emplace_back(allocator);
    return result;
}

std::expected<std::unique_ptr<SwapChain>, core::Error>
VKDevice::createSwapChain(const SwapChainDesc& desc) {
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

    auto result = VKSwapChain::create(desc, params, render_config_);
    if (!result) return std::unexpected(result.error());
    auto& swapchain = *result;

    // 为每个 swapchain image 创建独立的 renderFinished 信号量
    for (uint32_t i = static_cast<uint32_t>(render_finished_semaphores_.size());
         i < swapchain->imageCount(); ++i) {
        render_finished_semaphores_.push_back(device_.createSemaphore({}));
    }

    if (frame_contexts_.empty()) {
        initFrameContexts(frame_count_);
    }

    return result;
}

std::expected<std::unique_ptr<Fence>, core::Error>
VKDevice::createFence(uint64_t initialValue) {
    auto result = VKFence::create(device_, initialValue);
    if (!result) return std::unexpected(result.error());
    auto& f = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "Fence@%p", f.get());
    setDebugName(device_, vk::ObjectType::eSemaphore,
                 reinterpret_cast<uint64_t>(VkSemaphore(f->semaphore())), nm);
    return result;
}

std::expected<std::unique_ptr<BindGroup>, core::Error>
VKDevice::createBindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc) {
    return std::unique_ptr<BindGroup>(
        std::make_unique<VKBindGroup>(layout, desc.entries, desc.count));
}

void VKDevice::uploadTextureData(Texture* dst, const void* data,
                                 uint32_t width, uint32_t height,
                                 TextureFormat format) {
    upload_context_->uploadTexture(static_cast<VKTexture*>(dst), data,
                                   width, height, format);
}

std::expected<std::unique_ptr<RenderTarget>, core::Error>
VKDevice::createRenderTarget(const RenderTargetDesc& desc) {
    // 如果 frame contexts 还未初始化（无 SwapChain 时），在此初始化
    if (frame_contexts_.empty()) {
        initFrameContexts(frame_count_);
    }
    auto result = VKRenderTarget::create(desc, device_, allocator_);
    if (!result) return std::unexpected(result.error());
    auto& rt = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "RenderTarget@%p", rt.get());
    setDebugName(device_, vk::ObjectType::eRenderPass,
                 reinterpret_cast<uint64_t>(VkRenderPass(rt->renderPass())), nm);
    setDebugName(device_, vk::ObjectType::eFramebuffer,
                 reinterpret_cast<uint64_t>(VkFramebuffer(rt->framebuffer())), nm);
    return result;
}

std::expected<std::unique_ptr<Sampler>, core::Error>
VKDevice::createSampler(const SamplerDesc& desc) {
    auto result = VKSampler::create(desc, device_);
    if (!result) return std::unexpected(result.error());
    auto& s = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "Sampler@%p", s.get());
    setDebugName(device_, vk::ObjectType::eSampler,
                 reinterpret_cast<uint64_t>(VkSampler(s->handle())), nm);
    return result;
}

// ============================================================
// 提交命令
// ============================================================

void VKDevice::executeCommandLists(CommandList** cmdLists, uint32_t count,
                                   Fence* fence, uint64_t fenceValue) {
    // vulkan-hpp 的 submit() 为异常版：验证层发现的录制错误（缺 sampler、
    // layout 冲突等）会在这一步抛 vk::Error。公共签名是 void，异常若逃逸会
    // 变成未捕获崩溃。这里接住并记日志，保持提交失败可见而非静默崩溃。
    // 注意：真正的异步 GPU 执行错误（device lost）在此处查不到，需在
    // waitIdle / 下一帧 fence 检查时发现。
    std::vector<vk::CommandBuffer> cmdBuffers(count);
    for (uint32_t i = 0; i < count; ++i) {
        cmdBuffers[i] = static_cast<VKCommandList*>(cmdLists[i])->cmdBuffer();
    }

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = count;
    submitInfo.pCommandBuffers    = cmdBuffers.data();

    try {
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
    } catch (const vk::Error& e) {
        std::fprintf(stderr, "[VK ERROR] submit failed: %s\n", e.what());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[VK ERROR] submit failed (non-Vulkan): %s\n", e.what());
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

void VKDevice::beginFrame(SwapChain* swapchain) {
    auto& frame = currentFrameContext();
    frame.waitForFence();
    frame.resetFence();

    frame.resetCommandBuffer();

    descriptor_allocators_[current_frame_]->resetPools();

    // 延迟回收：上一帧的 standalone allocator 现在安全（其 cmd list 已通过
    // readbackPixels 的 fence wait 确认完成，或有足够时间让 GPU 执行完）。
    standalone_allocators_prev_.clear();
    // 当前帧的 allocator 挪到上一帧，下一帧回收
    standalone_allocators_prev_ = std::move(standalone_allocators_);
    standalone_allocators_.clear();

    frame_cmd_list_ = std::make_unique<VKCommandList>(device_, frame.cmdBuffer(),
                                                    descriptor_allocators_[current_frame_].get());
    frame_cmd_list_->setOwnerDevice(this);

    if (swapchain) {
        auto* sc = static_cast<VKSwapChain*>(swapchain);
        sc->acquireNextImage(frame.imageAvailable());
        acquired_image_index_ = sc->currentImageIndex();
    }
}

void VKDevice::clearCaches() {
    clearFramebufferCache();
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
