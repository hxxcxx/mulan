#include "VKCommandList.h"
#include "VKTexture.h"
#include "VKDescriptorAllocator.h"
#include "VKDevice.h"

namespace mulan::engine {

VKCommandList::VKCommandList(vk::Device device, uint32_t queueFamilyIndex)
    : m_device(device)
    , m_ownsPool(true)
{
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolCI.queueFamilyIndex = queueFamilyIndex;

    m_pool = m_device.createCommandPool(poolCI);

    vk::CommandBufferAllocateInfo allocCI;
    allocCI.commandPool        = m_pool;
    allocCI.level              = vk::CommandBufferLevel::ePrimary;
    allocCI.commandBufferCount = 1;

    auto cmdBuffers = m_device.allocateCommandBuffers(allocCI);
    m_cmdBuffer = cmdBuffers[0];
}

VKCommandList::VKCommandList(vk::Device device, uint32_t queueFamilyIndex,
                             VKDescriptorAllocator* allocator)
    : VKCommandList(device, queueFamilyIndex)
{
    m_allocator = allocator;
}

VKCommandList::VKCommandList(vk::Device device, vk::CommandBuffer externalCmd)
    : m_device(device)
    , m_cmdBuffer(externalCmd)
    , m_ownsPool(false)
{}

VKCommandList::VKCommandList(vk::Device device, vk::CommandBuffer externalCmd,
                             VKDescriptorAllocator* allocator)
    : m_device(device)
    , m_cmdBuffer(externalCmd)
    , m_allocator(allocator)
    , m_ownsPool(false)
{}

VKCommandList::~VKCommandList() {
    if (m_ownsPool && m_pool) {
        m_device.freeCommandBuffers(m_pool, m_cmdBuffer);
        m_device.destroyCommandPool(m_pool);
    }
}

void VKCommandList::begin() {
    if (m_ownsPool) {
        m_device.resetCommandPool(m_pool);
    }

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    m_cmdBuffer.begin(beginInfo);
}

void VKCommandList::end() {
    m_cmdBuffer.end();
}

void VKCommandList::setPipelineState(PipelineState* pso) {
    auto* vkPso = static_cast<VKPipelineState*>(pso);
    m_cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, vkPso->pipeline());
    m_currentLayout = vkPso->layout();
    m_currentDescSetLayout = vkPso->descriptorSetLayout();
}

void VKCommandList::setViewport(const Viewport& vp) {
    vk::Viewport viewport;
    viewport.x        = vp.x;
    viewport.y        = vp.y;
    viewport.width    = vp.width;
    viewport.height   = vp.height;
    viewport.minDepth = vp.minDepth;
    viewport.maxDepth = vp.maxDepth;
    m_cmdBuffer.setViewport(0, 1, &viewport);
}

void VKCommandList::setScissorRect(const ScissorRect& rect) {
    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D(rect.x, rect.y);
    scissor.extent = vk::Extent2D(static_cast<uint32_t>(rect.width),
                                   static_cast<uint32_t>(rect.height));
    m_cmdBuffer.setScissor(0, 1, &scissor);
}

void VKCommandList::setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    auto* vkBuf = static_cast<VKBuffer*>(buffer);
    vk::Buffer buf = vkBuf->vkBuffer();
    vk::DeviceSize offs = offset;
    m_cmdBuffer.bindVertexBuffers(slot, 1, &buf, &offs);
}

void VKCommandList::setVertexBuffers(uint32_t startSlot, uint32_t count,
                                      Buffer** buffers, uint32_t* offsets) {
    std::vector<vk::Buffer> bufs;
    std::vector<vk::DeviceSize> offs;
    for (uint32_t i = 0; i < count; ++i) {
        bufs.push_back(static_cast<VKBuffer*>(buffers[i])->vkBuffer());
        offs.push_back(offsets ? offsets[i] : 0);
    }
    m_cmdBuffer.bindVertexBuffers(startSlot, bufs, offs);
}

void VKCommandList::setIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) {
    auto* vkBuf = static_cast<VKBuffer*>(buffer);
    m_cmdBuffer.bindIndexBuffer(vkBuf->vkBuffer(), offset, toVkIndexType(type));
}

void VKCommandList::draw(const DrawAttribs& attribs) {
    m_cmdBuffer.draw(attribs.vertexCount, attribs.instanceCount,
                     attribs.startVertex, attribs.startInstance);
}

void VKCommandList::drawIndexed(const DrawIndexedAttribs& attribs) {
    m_cmdBuffer.drawIndexed(attribs.indexCount, attribs.instanceCount,
                            attribs.startIndex, attribs.baseVertex,
                            attribs.startInstance);
}

void VKCommandList::drawIndirect(Buffer* argsBuffer, uint32_t offset,
                                  uint32_t drawCount, uint32_t stride) {
    auto* vkBuf = static_cast<VKBuffer*>(argsBuffer);
    vk::Buffer buf = vkBuf->vkBuffer();
    m_cmdBuffer.drawIndexedIndirect(buf, offset, drawCount,
                                     stride > 0 ? stride : uint32_t(sizeof(VkDrawIndexedIndirectCommand)));
}

void VKCommandList::dispatch(uint32_t threadGroupX, uint32_t threadGroupY,
                              uint32_t threadGroupZ) {
    m_cmdBuffer.dispatch(threadGroupX, threadGroupY, threadGroupZ);
}

void VKCommandList::dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
    auto* vkBuf = static_cast<VKBuffer*>(argsBuffer);
    m_cmdBuffer.dispatchIndirect(vkBuf->vkBuffer(), offset);
}

void VKCommandList::setPushConstants(uint32_t offset, uint32_t size,
                                      const void* data, uint32_t stageFlags) {
    vk::ShaderStageFlags vkStages;
    if (stageFlags & PipelineBinding::kStageVertex)   vkStages |= vk::ShaderStageFlagBits::eVertex;
    if (stageFlags & PipelineBinding::kStageFragment) vkStages |= vk::ShaderStageFlagBits::eFragment;
    if (stageFlags & PipelineBinding::kStageCompute)  vkStages |= vk::ShaderStageFlagBits::eCompute;

    if (vkStages) {
        m_cmdBuffer.pushConstants(m_currentLayout, vkStages, offset, size, data);
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

    m_cmdBuffer.pipelineBarrier(srcStage, dstStage, {}, nullptr, nullptr, barrier);

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

    m_cmdBuffer.copyImageToBuffer(
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

    m_cmdBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
}

void VKCommandList::bindResources(const BindGroup& group) {
    if (group.count == 0 || !m_allocator) return;

    assert(m_currentDescSetLayout && "bindResources called before setPipelineState");
    if (!m_currentDescSetLayout) return;

    VKDescriptorSet set = m_allocator->allocate(m_currentDescSetLayout);

    for (uint8_t i = 0; i < group.count; ++i) {
        const auto& e = group.entries[i];
        if (e.buffer) {
            auto* vkBuf = static_cast<VKBuffer*>(e.buffer);
            set.writeUBO(e.binding, vkBuf->vkBuffer(), e.offset, e.size);
        } else if (e.texture) {
            auto* vkTex = static_cast<VKTexture*>(e.texture);
            set.writeSampledImage(e.binding, vkTex->view());
        }
    }

    set.flush();
    set.bind(m_cmdBuffer, m_currentLayout);
}

void VKCommandList::endRenderPass() {
    m_cmdBuffer.endRenderPass();
}

void VKCommandList::bindDescriptorSet(vk::PipelineLayout layout, vk::DescriptorSet set,
                                       uint32_t firstSet) {
    m_cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   layout, firstSet, 1, &set, 0, nullptr);
}

// ============================================================
// RHI beginRenderPass / endRenderPass
// ============================================================

void VKCommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    assert(m_ownerDevice && "VKCommandList needs ownerDevice for RenderPass/Framebuffer cache");

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
    vk::RenderPass renderPass = m_ownerDevice->getOrCreateRenderPass(
        std::span<const TextureFormat>(colorFmts.data(), info.colorCount),
        depthFmt, hasDepth, colorLoadOp, colorStoreOp, colorFinalLayout);

    // 从 device cache 获取 Framebuffer
    vk::Framebuffer framebuffer = m_ownerDevice->getOrCreateFramebuffer(
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

    m_cmdBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
}

} // namespace mulan::engine
