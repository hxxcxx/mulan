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
    // 通过 renderPass 的 clearValue 实现
}

void VKCommandList::beginVkRenderPass(vk::RenderPass renderPass, vk::Framebuffer framebuffer,
                                     uint32_t width, uint32_t height,
                                     const std::array<float, 4>& clearColor,
                                     float clearDepth) {
    vk::ClearValue clearValues[2];
    clearValues[0].color = vk::ClearColorValue(clearColor);
    clearValues[1].depthStencil = vk::ClearDepthStencilValue(clearDepth, 0);

    vk::RenderPassBeginInfo rpBegin;
    rpBegin.renderPass      = renderPass;
    rpBegin.framebuffer     = framebuffer;
    rpBegin.renderArea      = vk::Rect2D({0, 0}, {width, height});
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues    = clearValues;

    cmd_buffer_.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
}

void VKCommandList::bindGroup(BindGroup& group) {
    auto* vkGroup = static_cast<VKBindGroup*>(&group);
    if (vkGroup->entryCount() == 0 || !allocator_) return;
    if (!current_desc_set_layout_) return;

    // --- 尝试复用缓存的 descriptor set ---
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

    for (uint8_t i = 0; i < vkGroup->entryCount(); ++i) {
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
    }

    wrapper.flush();
    wrapper.bind(cmd_buffer_, current_layout_);
    vkGroup->markClean();
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

void VKCommandList::endRenderPass() {
    cmd_buffer_.endRenderPass();
}

void VKCommandList::bindDescriptorSet(vk::PipelineLayout layout, vk::DescriptorSet set,
                                       uint32_t firstSet) {
    cmd_buffer_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   layout, firstSet, 1, &set, 0, nullptr);
}

// ============================================================
// RHI beginRenderPass / endRenderPass
// ============================================================

void VKCommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    assert(owner_device_ && "VKCommandList needs ownerDevice for RenderPass/Framebuffer cache");

    // 收集 color attachment 的格式和 ImageView
    std::array<TextureFormat, RenderPassBeginInfo::kMaxColorTargets> colorFmts{};
    std::array<vk::ImageView, 9> views{};  // 8 color + 1 depth
    uint8_t viewCount = 0;

    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* tex = static_cast<VKTexture*>(info.colorAttachments[i].target);
        assert(tex && "Color attachment must not be null");
        colorFmts[i] = tex->format();
        views[viewCount++] = tex->view();
    }

    // Depth attachment
    TextureFormat depthFmt = TextureFormat::Unknown;
    bool hasDepth = (info.depthAttachment.target != nullptr);
    if (hasDepth) {
        auto* depthTex = static_cast<VKTexture*>(info.depthAttachment.target);
        depthFmt = depthTex->format();
        views[viewCount++] = depthTex->view();
    }

    // 确定 finalLayout 和 loadOp/storeOp
    vk::AttachmentLoadOp colorLoadOp = toVkLoadOp(info.colorAttachments[0].loadAction);
    vk::AttachmentStoreOp colorStoreOp = toVkStoreOp(info.colorAttachments[0].storeAction);
    vk::ImageLayout colorFinalLayout = info.presentSource
        ? vk::ImageLayout::ePresentSrcKHR
        : vk::ImageLayout::eShaderReadOnlyOptimal;

    // 从 device cache 获取 RenderPass
    vk::RenderPass renderPass = owner_device_->getOrCreateRenderPass(
        std::span<const TextureFormat>(colorFmts.data(), info.colorCount),
        depthFmt, hasDepth, colorLoadOp, colorStoreOp, colorFinalLayout);

    // 从 device cache 获取 Framebuffer
    vk::Framebuffer framebuffer = owner_device_->getOrCreateFramebuffer(
        renderPass,
        std::span<const vk::ImageView>(views.data(), viewCount),
        info.width, info.height);

    // 构造 clear values
    std::array<vk::ClearValue, 9> clearValues;
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        clearValues[i].color = vk::ClearColorValue(std::array<float, 4>{
            info.clearColor[0], info.clearColor[1], info.clearColor[2], info.clearColor[3]});
    }
    if (hasDepth) {
        clearValues[info.colorCount].depthStencil =
            vk::ClearDepthStencilValue(info.clearDepth, info.clearStencil);
    }

    // 录制 vkCmdBeginRenderPass
    vk::RenderPassBeginInfo rpBegin;
    rpBegin.renderPass      = renderPass;
    rpBegin.framebuffer     = framebuffer;
    rpBegin.renderArea      = vk::Rect2D({0, 0}, {info.width, info.height});
    rpBegin.clearValueCount = info.colorCount + (hasDepth ? 1 : 0);
    rpBegin.pClearValues    = clearValues.data();

    cmd_buffer_.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
}

} // namespace mulan::engine
