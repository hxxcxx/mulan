#include "vk_command_list.h"
#include "vk_texture.h"
#include "vk_sampler.h"
#include "vk_descriptor_allocator.h"
#include "vk_device.h"
#include "vk_bind_group.h"

#include <mulan/core/result/error.h>
#include "../../engine_error_code.h"

#include <string>

namespace mulan::engine {

std::expected<std::unique_ptr<VKCommandList>, core::Error>
VKCommandList::create(vk::Device device, uint32_t queueFamilyIndex,
                      VKDescriptorAllocator* allocator) {
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolCI.queueFamilyIndex = queueFamilyIndex;

    vk::CommandPool pool;
    vk::CommandBuffer cmd;
    try {
        pool = device.createCommandPool(poolCI);

        vk::CommandBufferAllocateInfo allocCI;
        allocCI.commandPool        = pool;
        allocCI.level              = vk::CommandBufferLevel::ePrimary;
        allocCI.commandBufferCount = 1;
        auto cmdBuffers = device.allocateCommandBuffers(allocCI);
        cmd = cmdBuffers[0];
    } catch (const vk::Error& e) {
        return std::unexpected(makeError(EngineErrorCode::CommandListCreateFailed,
            std::string("VKCommandList create failed: ") + e.what()));
    }

    auto obj = std::unique_ptr<VKCommandList>(new VKCommandList(device, pool, cmd));
    obj->allocator_ = allocator;
    return obj;
}

VKCommandList::VKCommandList(vk::Device device, vk::CommandBuffer externalCmd)
    : device_(device)
    , cmd_buffer_(externalCmd)
    , owns_pool_(false)
{}

VKCommandList::VKCommandList(vk::Device device, vk::CommandBuffer externalCmd,
                             VKDescriptorAllocator* allocator)
    : device_(device)
    , cmd_buffer_(externalCmd)
    , allocator_(allocator)
    , owns_pool_(false)
{}

VKCommandList::~VKCommandList() {
    if (owns_pool_ && pool_) {
        device_.freeCommandBuffers(pool_, cmd_buffer_);
        device_.destroyCommandPool(pool_);
    }
}

void VKCommandList::begin() {
    if (owns_pool_) {
        device_.resetCommandPool(pool_);
    }

    swapchain_color_image_.reset();
    rp_present_source_ = false;

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd_buffer_.begin(beginInfo);
}

void VKCommandList::end() {
    cmd_buffer_.end();
}

void VKCommandList::setPipelineState(PipelineState* pso) {
    auto* vkPso = static_cast<VKPipelineState*>(pso);
    cmd_buffer_.bindPipeline(vk::PipelineBindPoint::eGraphics, vkPso->pipeline());
    current_layout_ = vkPso->layout();
    current_desc_set_layout_ = vkPso->descriptorSetLayout();
}

void VKCommandList::setViewport(const Viewport& vp) {
    vk::Viewport viewport;
    viewport.x        = vp.x;
    viewport.y        = vp.y;
    viewport.width    = vp.width;
    viewport.height   = vp.height;
    viewport.minDepth = vp.minDepth;
    viewport.maxDepth = vp.maxDepth;
    cmd_buffer_.setViewport(0, 1, &viewport);
}

void VKCommandList::setScissorRect(const ScissorRect& rect) {
    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D(rect.x, rect.y);
    scissor.extent = vk::Extent2D(static_cast<uint32_t>(rect.width),
                                   static_cast<uint32_t>(rect.height));
    cmd_buffer_.setScissor(0, 1, &scissor);
}

void VKCommandList::setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    auto* vkBuf = static_cast<VKBuffer*>(buffer);
    vk::Buffer buf = vkBuf->vkBuffer();
    vk::DeviceSize offs = offset;
    cmd_buffer_.bindVertexBuffers(slot, 1, &buf, &offs);
}

void VKCommandList::setVertexBuffers(uint32_t startSlot, uint32_t count,
                                      Buffer** buffers, uint32_t* offsets) {
    std::vector<vk::Buffer> bufs;
    std::vector<vk::DeviceSize> offs;
    for (uint32_t i = 0; i < count; ++i) {
        bufs.push_back(static_cast<VKBuffer*>(buffers[i])->vkBuffer());
        offs.push_back(offsets ? offsets[i] : 0);
    }
    cmd_buffer_.bindVertexBuffers(startSlot, bufs, offs);
}

void VKCommandList::setIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) {
    auto* vkBuf = static_cast<VKBuffer*>(buffer);
    cmd_buffer_.bindIndexBuffer(vkBuf->vkBuffer(), offset, toVkIndexType(type));
}

void VKCommandList::draw(const DrawAttribs& attribs) {
    cmd_buffer_.draw(attribs.vertexCount, attribs.instanceCount,
                     attribs.startVertex, attribs.startInstance);
}

void VKCommandList::drawIndexed(const DrawIndexedAttribs& attribs) {
    cmd_buffer_.drawIndexed(attribs.indexCount, attribs.instanceCount,
                            attribs.startIndex, attribs.baseVertex,
                            attribs.startInstance);
}

void VKCommandList::drawIndirect(Buffer* argsBuffer, uint32_t offset,
                                  uint32_t drawCount, uint32_t stride) {
    auto* vkBuf = static_cast<VKBuffer*>(argsBuffer);
    vk::Buffer buf = vkBuf->vkBuffer();
    cmd_buffer_.drawIndexedIndirect(buf, offset, drawCount,
                                     stride > 0 ? stride : uint32_t(sizeof(VkDrawIndexedIndirectCommand)));
}

void VKCommandList::dispatch(uint32_t threadGroupX, uint32_t threadGroupY,
                              uint32_t threadGroupZ) {
    cmd_buffer_.dispatch(threadGroupX, threadGroupY, threadGroupZ);
}

void VKCommandList::dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
    auto* vkBuf = static_cast<VKBuffer*>(argsBuffer);
    cmd_buffer_.dispatchIndirect(vkBuf->vkBuffer(), offset);
}

void VKCommandList::setPushConstants(uint32_t offset, uint32_t size,
                                      const void* data, uint32_t stageFlags) {
    vk::ShaderStageFlags vkStages;
    if (stageFlags & PipelineBinding::kStageVertex)   vkStages |= vk::ShaderStageFlagBits::eVertex;
    if (stageFlags & PipelineBinding::kStageFragment) vkStages |= vk::ShaderStageFlagBits::eFragment;
    if (stageFlags & PipelineBinding::kStageCompute)  vkStages |= vk::ShaderStageFlagBits::eCompute;

    if (vkStages) {
        cmd_buffer_.pushConstants(current_layout_, vkStages, offset, size, data);
    }
}

void VKCommandList::updateBuffer(Buffer* buffer, uint32_t offset,
                                  uint32_t size, const void* data,
                                  ResourceTransitionMode) {
    auto* vkBuf = static_cast<VKBuffer*>(buffer);
    vkBuf->update(offset, size, data);
}

void VKCommandList::transitionResource(Buffer*, ResourceState) {
    // Vulkan 通过 pipeline barrier 处理，此处简化
}

void VKCommandList::transitionResource(Texture* texture, ResourceState newState) {
    auto* vkTex = static_cast<VKTexture*>(texture);

    vk::ImageMemoryBarrier barrier;
    barrier.image = vkTex->image();
    barrier.subresourceRange.aspectMask = VKTexture::isDepthFormat(vkTex->desc().format)
        ? (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)
        : vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = vkTex->desc().mipLevels;
    barrier.subresourceRange.baseArrayLayer  = 0;
    barrier.subresourceRange.layerCount     = vkTex->desc().arraySize;

    // 使用纹理跟踪的当前布局，避免丢弃已有内容
    barrier.oldLayout    = vkTex->currentLayout();
    barrier.srcAccessMask = {};

    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eAllCommands;
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eAllCommands;

    switch (newState) {
        case ResourceState::RenderTarget:
            barrier.newLayout    = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
            dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            break;
        case ResourceState::ShaderResource:
            barrier.newLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dstStage = vk::PipelineStageFlagBits::eFragmentShader;
            break;
        case ResourceState::CopySrc:
            barrier.newLayout    = vk::ImageLayout::eTransferSrcOptimal;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dstStage = vk::PipelineStageFlagBits::eTransfer;
            break;
        case ResourceState::CopyDest:
            barrier.newLayout    = vk::ImageLayout::eTransferDstOptimal;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
            dstStage = vk::PipelineStageFlagBits::eTransfer;
            break;
        case ResourceState::DepthWrite:
            barrier.newLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
            dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
            break;
        default:
            barrier.newLayout = vk::ImageLayout::eGeneral;
            break;
    }

    cmd_buffer_.pipelineBarrier(srcStage, dstStage, {}, nullptr, nullptr, barrier);

    // 更新纹理的布局跟踪
    vkTex->setCurrentLayout(barrier.newLayout);
}

void VKCommandList::copyTextureToBuffer(Texture* src, Buffer* dst) {
    auto* vkTex = static_cast<VKTexture*>(src);
    auto* vkBuf = static_cast<VKBuffer*>(dst);

    vk::BufferImageCopy region;
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;  // 紧密排列
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset = vk::Offset3D(0, 0, 0);
    region.imageExtent = vk::Extent3D(vkTex->desc().width, vkTex->desc().height, 1);

    cmd_buffer_.copyImageToBuffer(
        vkTex->image(), vk::ImageLayout::eTransferSrcOptimal,
        vkBuf->vkBuffer(), 1, &region);
}

void VKCommandList::clearColor(float r, float g, float b, float a) {
    // 通过 renderPass 的 clearValue 实现
}

void VKCommandList::clearDepth(float) {
    // 通过 renderPass 的 clearValue 实现
}

void VKCommandList::clearStencil(uint8_t) {
    // 由 VkRenderingAttachmentInfo 的 clearValue 实现
}

void VKCommandList::bindGroup(BindGroup& group) {
    auto* vkGroup = static_cast<VKBindGroup*>(&group);
    if (vkGroup->entryCount() == 0 || !allocator_) return;
    if (!current_desc_set_layout_) return;

    // --- 跨帧失效：per-frame pool reset 已销毁上一帧的 descriptor set，
    // 若 BindGroup 缓存句柄不属于当前帧则丢弃并强制本帧完整重写。
    if (vkGroup->frameToken() != frame_token_) {
        vkGroup->setCachedSet(nullptr);
        vkGroup->setFrameToken(frame_token_);
        vkGroup->markAllDirty();
    }

    // --- 复用未变脏的缓存 descriptor set ---
    if (!vkGroup->dirty() && vkGroup->cachedSet()) {
        auto dset = vkGroup->cachedSet();
        cmd_buffer_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       current_layout_, 0, 1, &dset, 0, nullptr);
        return;
    }

    // --- 分配或复用 descriptor set ---
    vk::DescriptorSet dset = vkGroup->cachedSet();
    if (!dset) {
        VKDescriptorSet newSet = allocator_->allocate(current_desc_set_layout_);
        dset = newSet.vkSet();
        vkGroup->setCachedSet(dset);
    }

    VKDescriptorSet wrapper(allocator_->device(), dset);

    // 仅重写脏 binding（局部更新）；非脏 binding 复用上一轮已写入的 descriptor
    const uint16_t mask = vkGroup->dirtyMask();
    uint16_t written = 0;
    for (uint8_t i = 0; i < vkGroup->entryCount(); ++i) {
        if (((mask >> i) & 1u) == 0) continue;
        const auto& e = vkGroup->entries()[i];
        if (e.buffer) {
            auto* vkBuf = static_cast<VKBuffer*>(e.buffer);
            wrapper.writeUBO(e.binding, vkBuf->vkBuffer(), e.offset, e.size);
        } else if (e.texture) {
            auto* vkTex = static_cast<VKTexture*>(e.texture);
            wrapper.writeSampledImage(e.binding, vkTex->view());
        } else if (e.sampler) {
            auto* vkSm = static_cast<VKSampler*>(e.sampler);
            wrapper.writeSampler(e.binding, vkSm->handle());
        }
        written |= (uint16_t(1) << i);
    }

    wrapper.flush();
    wrapper.bind(cmd_buffer_, current_layout_);
    vkGroup->clearDirty(written);
}

void VKCommandList::bindResources(const BindGroupDesc& desc) {
    if (desc.count == 0 || !allocator_) return;
    if (!current_desc_set_layout_) return;

    VKDescriptorSet set = allocator_->allocate(current_desc_set_layout_);

    for (uint8_t i = 0; i < desc.count; ++i) {
        const auto& e = desc.entries[i];
        if (e.buffer) {
            auto* vkBuf = static_cast<VKBuffer*>(e.buffer);
            set.writeUBO(e.binding, vkBuf->vkBuffer(), e.offset, e.size);
        } else if (e.texture) {
            auto* vkTex = static_cast<VKTexture*>(e.texture);
            set.writeSampledImage(e.binding, vkTex->view());
        } else if (e.sampler) {
            auto* vkSm = static_cast<VKSampler*>(e.sampler);
            set.writeSampler(e.binding, vkSm->handle());
        }
    }

    set.flush();
    set.bind(cmd_buffer_, current_layout_);
}

// ============================================================
// RHI beginRenderPass / endRenderPass
// ============================================================

void VKCommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    rp_present_source_ = info.presentSource;

    // Track swapchain color image for present transition in endRenderPass
    if (rp_present_source_ && info.colorCount > 0 && info.colorAttachments[0].target) {
        swapchain_color_image_ = static_cast<VKTexture*>(
            info.colorAttachments[0].target)->image();
    }

    // Color attachment barriers: transition to COLOR_ATTACHMENT_OPTIMAL
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* tex = static_cast<VKTexture*>(info.colorAttachments[i].target);
        vk::ImageMemoryBarrier barrier;
        barrier.image               = tex->image();
        barrier.oldLayout           = vk::ImageLayout::eUndefined;
        barrier.newLayout           = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.srcAccessMask       = {};
        barrier.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.subresourceRange    = vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;

        cmd_buffer_.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, nullptr, nullptr, barrier);
    }

    // Depth attachment barrier: transition to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    if (info.depthAttachment.target) {
        auto* depthTex = static_cast<VKTexture*>(info.depthAttachment.target);
        vk::ImageMemoryBarrier barrier;
        barrier.image               = depthTex->image();
        barrier.oldLayout           = vk::ImageLayout::eUndefined;
        barrier.newLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barrier.srcAccessMask       = {};
        barrier.dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.subresourceRange    = vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
            0, 1, 0, 1);
        barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;

        cmd_buffer_.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {}, nullptr, nullptr, barrier);
    }

    // Build dynamic rendering attachments
    std::array<vk::RenderingAttachmentInfo, RenderPassBeginInfo::kMaxColorTargets> colorAtt{};
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* tex = static_cast<VKTexture*>(info.colorAttachments[i].target);
        auto& att = colorAtt[i];
        att.imageView   = tex->view();
        att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        att.loadOp      = (info.colorAttachments[i].loadAction == LoadAction::Clear)
            ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
        att.storeOp     = vk::AttachmentStoreOp::eStore;
        att.clearValue.color = vk::ClearColorValue(std::array<float, 4>{
            info.clearColor[0], info.clearColor[1], info.clearColor[2], info.clearColor[3]});
    }

    vk::RenderingAttachmentInfo depthAtt{};
    if (info.depthAttachment.target) {
        auto* depthTex = static_cast<VKTexture*>(info.depthAttachment.target);
        depthAtt.imageView   = depthTex->view();
        depthAtt.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAtt.loadOp      = (info.depthAttachment.loadAction == LoadAction::Clear)
            ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
        depthAtt.storeOp     = vk::AttachmentStoreOp::eStore;
        depthAtt.clearValue.depthStencil = vk::ClearDepthStencilValue(info.clearDepth, info.clearStencil);
    }

    vk::RenderingInfo renderingInfo;
    renderingInfo.renderArea           = vk::Rect2D({0, 0}, {info.width, info.height});
    renderingInfo.layerCount           = 1;
    renderingInfo.colorAttachmentCount = info.colorCount;
    renderingInfo.pColorAttachments    = colorAtt.data();
    renderingInfo.pDepthAttachment     = info.depthAttachment.target ? &depthAtt : nullptr;
    renderingInfo.pStencilAttachment   = nullptr;

    cmd_buffer_.beginRendering(renderingInfo);
}

void VKCommandList::endRenderPass() {
    cmd_buffer_.endRendering();

    // Swapchain present: transition color attachment to PRESENT_SRC_KHR
    if (rp_present_source_ && swapchain_color_image_) {
        vk::ImageMemoryBarrier barrier;
        barrier.image               = *swapchain_color_image_;
        barrier.oldLayout           = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.newLayout           = vk::ImageLayout::ePresentSrcKHR;
        barrier.srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.dstAccessMask       = {};
        barrier.subresourceRange    = vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;

        cmd_buffer_.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, nullptr, nullptr, barrier);

        swapchain_color_image_.reset();
    }
    rp_present_source_ = false;
}

} // namespace mulan::engine
