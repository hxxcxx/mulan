#include "detail/vk_command_list.h"
#include "detail/vk_texture.h"
#include "detail/vk_sampler.h"
#include "detail/vk_descriptor_allocator.h"
#include "detail/vk_device.h"
#include "detail/vk_bind_group.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <string>

namespace mulan::engine {

namespace {

struct VKImageAccessInfo {
    vk::PipelineStageFlags stages{};
    vk::AccessFlags access{};
};

VKImageAccessInfo accessInfoForLayout(vk::ImageLayout layout) {
    switch (layout) {
    case vk::ImageLayout::eUndefined: return { vk::PipelineStageFlagBits::eTopOfPipe, {} };
    case vk::ImageLayout::eColorAttachmentOptimal:
        return { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite };
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        return { vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
                 vk::AccessFlagBits::eDepthStencilAttachmentWrite };
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
        return { vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
                 vk::AccessFlagBits::eDepthStencilAttachmentRead };
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return { vk::PipelineStageFlagBits::eAllGraphics | vk::PipelineStageFlagBits::eComputeShader,
                 vk::AccessFlagBits::eShaderRead };
    case vk::ImageLayout::eTransferSrcOptimal:
        return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead };
    case vk::ImageLayout::eTransferDstOptimal:
        return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite };
    case vk::ImageLayout::ePresentSrcKHR: return { vk::PipelineStageFlagBits::eBottomOfPipe, {} };
    case vk::ImageLayout::eGeneral:
        return { vk::PipelineStageFlagBits::eAllCommands,
                 vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite };
    default: return { vk::PipelineStageFlagBits::eAllCommands, {} };
    }
}

vk::ImageLayout imageLayoutForState(ResourceState state) {
    switch (state) {
    case ResourceState::ShaderResource: return vk::ImageLayout::eShaderReadOnlyOptimal;
    case ResourceState::UnorderedAccess: return vk::ImageLayout::eGeneral;
    case ResourceState::RenderTarget: return vk::ImageLayout::eColorAttachmentOptimal;
    case ResourceState::DepthWrite: return vk::ImageLayout::eDepthStencilAttachmentOptimal;
    case ResourceState::DepthRead: return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    case ResourceState::Present: return vk::ImageLayout::ePresentSrcKHR;
    case ResourceState::CopyDest: return vk::ImageLayout::eTransferDstOptimal;
    case ResourceState::CopySrc: return vk::ImageLayout::eTransferSrcOptimal;
    case ResourceState::Common:
    case ResourceState::VertexBuffer:
    case ResourceState::IndexBuffer:
    case ResourceState::UniformBuffer: return vk::ImageLayout::eGeneral;
    default: return vk::ImageLayout::eGeneral;
    }
}

VKImageAccessInfo accessInfoForState(ResourceState state) {
    switch (state) {
    case ResourceState::ShaderResource:
        return { vk::PipelineStageFlagBits::eAllGraphics | vk::PipelineStageFlagBits::eComputeShader,
                 vk::AccessFlagBits::eShaderRead };
    case ResourceState::UnorderedAccess:
        return { vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
                 vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite };
    case ResourceState::RenderTarget:
        return { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite };
    case ResourceState::DepthWrite:
        return { vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
                 vk::AccessFlagBits::eDepthStencilAttachmentWrite };
    case ResourceState::DepthRead:
        return { vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
                 vk::AccessFlagBits::eDepthStencilAttachmentRead };
    case ResourceState::CopyDest: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite };
    case ResourceState::CopySrc: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead };
    case ResourceState::Present: return { vk::PipelineStageFlagBits::eBottomOfPipe, {} };
    case ResourceState::Common: return { vk::PipelineStageFlagBits::eAllCommands, {} };
    case ResourceState::VertexBuffer:
    case ResourceState::IndexBuffer:
    case ResourceState::UniformBuffer:
    default: return { vk::PipelineStageFlagBits::eAllCommands, {} };
    }
}

vk::ImageAspectFlags aspectMaskForTexture(TextureFormat format) {
    if (!VKTexture::isDepthFormat(format))
        return vk::ImageAspectFlagBits::eColor;
    if (format == TextureFormat::D24_UNorm_S8_UInt || format == TextureFormat::D32_Float_S8X24_UInt)
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    return vk::ImageAspectFlagBits::eDepth;
}

}  // namespace

core::Result<std::unique_ptr<VKCommandList>> VKCommandList::create(vk::Device device, uint32_t queueFamilyIndex,
                                                                   VKDescriptorAllocator* allocator) {
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolCI.queueFamilyIndex = queueFamilyIndex;

    vk::CommandPool pool;
    vk::CommandBuffer cmd;
    try {
        pool = device.createCommandPool(poolCI);

        vk::CommandBufferAllocateInfo allocCI;
        allocCI.commandPool = pool;
        allocCI.level = vk::CommandBufferLevel::ePrimary;
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
    : device_(device), cmd_buffer_(externalCmd), owns_pool_(false) {
}

VKCommandList::VKCommandList(vk::Device device, vk::CommandBuffer externalCmd, VKDescriptorAllocator* allocator)
    : device_(device), cmd_buffer_(externalCmd), allocator_(allocator), owns_pool_(false) {
}

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

    swapchain_color_texture_ = nullptr;
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
    viewport.x = vp.x;
    viewport.y = vp.y;
    viewport.width = vp.width;
    viewport.height = vp.height;
    viewport.minDepth = vp.minDepth;
    viewport.maxDepth = vp.maxDepth;
    cmd_buffer_.setViewport(0, 1, &viewport);
}

void VKCommandList::setScissorRect(const ScissorRect& rect) {
    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D(rect.x, rect.y);
    scissor.extent = vk::Extent2D(static_cast<uint32_t>(rect.width), static_cast<uint32_t>(rect.height));
    cmd_buffer_.setScissor(0, 1, &scissor);
}

void VKCommandList::setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    auto* vkBuf = static_cast<VKBuffer*>(buffer);
    vk::Buffer buf = vkBuf->vkBuffer();
    vk::DeviceSize offs = offset;
    cmd_buffer_.bindVertexBuffers(slot, 1, &buf, &offs);
}

void VKCommandList::setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) {
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
    cmd_buffer_.draw(attribs.vertexCount, attribs.instanceCount, attribs.startVertex, attribs.startInstance);
}

void VKCommandList::drawIndexed(const DrawIndexedAttribs& attribs) {
    cmd_buffer_.drawIndexed(attribs.indexCount, attribs.instanceCount, attribs.startIndex, attribs.baseVertex,
                            attribs.startInstance);
}

void VKCommandList::drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) {
    auto* vkBuf = static_cast<VKBuffer*>(argsBuffer);
    vk::Buffer buf = vkBuf->vkBuffer();
    cmd_buffer_.drawIndexedIndirect(buf, offset, drawCount,
                                    stride > 0 ? stride : uint32_t(sizeof(VkDrawIndexedIndirectCommand)));
}

void VKCommandList::dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
    cmd_buffer_.dispatch(threadGroupX, threadGroupY, threadGroupZ);
}

void VKCommandList::dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
    auto* vkBuf = static_cast<VKBuffer*>(argsBuffer);
    cmd_buffer_.dispatchIndirect(vkBuf->vkBuffer(), offset);
}

void VKCommandList::setPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) {
    vk::ShaderStageFlags vkStages;
    if (stageFlags & PipelineBinding::kStageVertex)
        vkStages |= vk::ShaderStageFlagBits::eVertex;
    if (stageFlags & PipelineBinding::kStageFragment)
        vkStages |= vk::ShaderStageFlagBits::eFragment;
    if (stageFlags & PipelineBinding::kStageCompute)
        vkStages |= vk::ShaderStageFlagBits::eCompute;

    if (vkStages) {
        cmd_buffer_.pushConstants(current_layout_, vkStages, offset, size, data);
    }
}

void VKCommandList::updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size, const void* data,
                                 ResourceTransitionMode) {
    auto* vkBuf = static_cast<VKBuffer*>(buffer);
    vkBuf->update(offset, size, data);
}

void VKCommandList::transitionResource(Buffer*, ResourceState) {
    // Vulkan 通过 pipeline barrier 处理，此处简化
}

void VKCommandList::transitionResource(Texture* texture, ResourceState newState) {
    auto* vkTex = static_cast<VKTexture*>(texture);
    const vk::ImageLayout oldLayout = vkTex->currentLayout();
    const vk::ImageLayout newLayout = imageLayoutForState(newState);
    if (oldLayout == newLayout)
        return;

    const auto srcInfo = accessInfoForLayout(oldLayout);
    const auto dstInfo = accessInfoForState(newState);

    vk::ImageMemoryBarrier barrier;
    barrier.image = vkTex->image();
    barrier.subresourceRange.aspectMask = aspectMaskForTexture(vkTex->desc().format);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = vkTex->desc().mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = vkTex->desc().arraySize;

    // 使用纹理跟踪的当前布局，避免丢弃已有内容
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = srcInfo.access;
    barrier.dstAccessMask = dstInfo.access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    cmd_buffer_.pipelineBarrier(srcInfo.stages, dstInfo.stages, {}, nullptr, nullptr, barrier);

    // 更新纹理的布局跟踪
    vkTex->setCurrentLayout(barrier.newLayout);
}

bool VKCommandList::copyTextureToBuffer(Texture* src, Buffer* dst) {
    auto* vkTex = static_cast<VKTexture*>(src);
    auto* vkBuf = static_cast<VKBuffer*>(dst);
    if (!vkTex || !vkBuf)
        return false;

    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // 紧密排列
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D(0, 0, 0);
    region.imageExtent = vk::Extent3D(vkTex->desc().width, vkTex->desc().height, 1);

    cmd_buffer_.copyImageToBuffer(vkTex->image(), vk::ImageLayout::eTransferSrcOptimal, vkBuf->vkBuffer(), 1, &region);
    return true;
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
    if (vkGroup->entryCount() == 0 || !allocator_)
        return;
    if (!current_desc_set_layout_)
        return;

    // --- 跨帧失效：per-frame pool reset 已销毁上一帧的 descriptor set，
    // 若 BindGroup 缓存句柄不属于当前帧则丢弃并强制本帧完整重写。
    if (vkGroup->frameToken() != frame_token_) {
        vkGroup->setCachedSet(nullptr);
        vkGroup->setFrameToken(frame_token_);
        vkGroup->markAllDirty();
    }

    // --- 完全复用：未变脏且已有缓存 set，直接 bind 缓存句柄，零分配零写入。
    // 这覆盖"同帧内多次 bind 同一个未改动的 BindGroup"场景。
    if (!vkGroup->dirty() && vkGroup->cachedSet()) {
        auto dset = vkGroup->cachedSet();
        cmd_buffer_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, current_layout_, 0, 1, &dset, 0, nullptr);
        return;
    }

    // --- 脏路径：分配全新的 descriptor set 并写入全部 binding。
    // Vulkan 规范禁止更新已 bind 到命令缓冲区的 descriptor set（除非 pool 带
    // UPDATE_AFTER_BIND，本引擎的 per-frame pool 不带该 flag）。因此脏时必须
    // 分配新 set，把所有 binding 完整写入，再 bind 新 set。
    vk::DescriptorSet dset = allocator_->allocate(current_desc_set_layout_).vkSet();
    vkGroup->setCachedSet(dset);

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
    if (desc.count == 0 || !allocator_)
        return;
    if (!current_desc_set_layout_)
        return;

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
        Texture* presentTexture = info.colorAttachments[0].resolveTarget ? info.colorAttachments[0].resolveTarget
                                                                         : info.colorAttachments[0].target;
        swapchain_color_texture_ = static_cast<VKTexture*>(presentTexture);
    }

    // Color attachment barriers: transition to COLOR_ATTACHMENT_OPTIMAL
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* tex = static_cast<VKTexture*>(info.colorAttachments[i].target);
        const vk::ImageLayout oldLayout = tex->currentLayout();
        const vk::ImageLayout newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        if (oldLayout != newLayout) {
            const auto srcInfo = accessInfoForLayout(oldLayout);
            const auto dstInfo = accessInfoForState(ResourceState::RenderTarget);
            vk::ImageMemoryBarrier barrier;
            barrier.image = tex->image();
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcAccessMask = srcInfo.access;
            barrier.dstAccessMask = dstInfo.access;
            barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            cmd_buffer_.pipelineBarrier(srcInfo.stages, dstInfo.stages, {}, nullptr, nullptr, barrier);
        }
        tex->setCurrentLayout(vk::ImageLayout::eColorAttachmentOptimal);

        if (info.colorAttachments[i].resolveTarget) {
            auto* resolveTex = static_cast<VKTexture*>(info.colorAttachments[i].resolveTarget);
            const vk::ImageLayout resolveOldLayout = resolveTex->currentLayout();
            if (resolveOldLayout != vk::ImageLayout::eColorAttachmentOptimal) {
                const auto srcInfo = accessInfoForLayout(resolveOldLayout);
                const auto dstInfo = accessInfoForState(ResourceState::RenderTarget);
                vk::ImageMemoryBarrier resolveBarrier;
                resolveBarrier.image = resolveTex->image();
                resolveBarrier.oldLayout = resolveOldLayout;
                resolveBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
                resolveBarrier.srcAccessMask = srcInfo.access;
                resolveBarrier.dstAccessMask = dstInfo.access;
                resolveBarrier.subresourceRange =
                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                resolveBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                resolveBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                cmd_buffer_.pipelineBarrier(srcInfo.stages, dstInfo.stages, {}, nullptr, nullptr, resolveBarrier);
            }
            resolveTex->setCurrentLayout(vk::ImageLayout::eColorAttachmentOptimal);
        }
    }

    // Depth attachment barrier: transition to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    if (info.depthAttachment.target) {
        auto* depthTex = static_cast<VKTexture*>(info.depthAttachment.target);
        const vk::ImageLayout oldLayout = depthTex->currentLayout();
        const vk::ImageLayout newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        if (oldLayout != newLayout) {
            const auto srcInfo = accessInfoForLayout(oldLayout);
            const auto dstInfo = accessInfoForState(ResourceState::DepthWrite);
            vk::ImageMemoryBarrier barrier;
            barrier.image = depthTex->image();
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcAccessMask = srcInfo.access;
            barrier.dstAccessMask = dstInfo.access;
            barrier.subresourceRange =
                    vk::ImageSubresourceRange(aspectMaskForTexture(depthTex->desc().format), 0, 1, 0, 1);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            cmd_buffer_.pipelineBarrier(srcInfo.stages, dstInfo.stages, {}, nullptr, nullptr, barrier);
        }
        depthTex->setCurrentLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    // Build dynamic rendering attachments
    std::array<vk::RenderingAttachmentInfo, RenderPassBeginInfo::kMaxColorTargets> colorAtt{};
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* tex = static_cast<VKTexture*>(info.colorAttachments[i].target);
        auto& att = colorAtt[i];
        att.imageView = tex->view();
        att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        att.loadOp = (info.colorAttachments[i].loadAction == LoadAction::Clear) ? vk::AttachmentLoadOp::eClear
                                                                                : vk::AttachmentLoadOp::eLoad;
        att.storeOp = toVkStoreOp(info.colorAttachments[i].storeAction);
        if (info.colorAttachments[i].resolveTarget) {
            auto* resolveTex = static_cast<VKTexture*>(info.colorAttachments[i].resolveTarget);
            att.resolveMode = vk::ResolveModeFlagBits::eAverage;
            att.resolveImageView = resolveTex->view();
            att.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        }
        att.clearValue.color = vk::ClearColorValue(
                std::array<float, 4>{ info.clearColor[0], info.clearColor[1], info.clearColor[2], info.clearColor[3] });
    }

    vk::RenderingAttachmentInfo depthAtt{};
    if (info.depthAttachment.target) {
        auto* depthTex = static_cast<VKTexture*>(info.depthAttachment.target);
        depthAtt.imageView = depthTex->view();
        depthAtt.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAtt.loadOp = (info.depthAttachment.loadAction == LoadAction::Clear) ? vk::AttachmentLoadOp::eClear
                                                                                 : vk::AttachmentLoadOp::eLoad;
        depthAtt.storeOp = toVkStoreOp(info.depthAttachment.storeAction);
        depthAtt.clearValue.depthStencil = vk::ClearDepthStencilValue(info.clearDepth, info.clearStencil);
    }

    vk::RenderingInfo renderingInfo;
    renderingInfo.renderArea = vk::Rect2D({ 0, 0 }, { info.width, info.height });
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = info.colorCount;
    renderingInfo.pColorAttachments = colorAtt.data();
    renderingInfo.pDepthAttachment = info.depthAttachment.target ? &depthAtt : nullptr;
    renderingInfo.pStencilAttachment = nullptr;

    cmd_buffer_.beginRendering(renderingInfo);
}

void VKCommandList::endRenderPass() {
    cmd_buffer_.endRendering();

    // Swapchain present: transition color attachment to PRESENT_SRC_KHR
    if (rp_present_source_ && swapchain_color_texture_) {
        auto* presentTexture = swapchain_color_texture_;
        vk::ImageMemoryBarrier barrier;
        barrier.image = presentTexture->image();
        barrier.oldLayout = presentTexture->currentLayout();
        barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
        const auto srcInfo = accessInfoForLayout(barrier.oldLayout);
        const auto dstInfo = accessInfoForState(ResourceState::Present);
        barrier.srcAccessMask = srcInfo.access;
        barrier.dstAccessMask = dstInfo.access;
        barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        if (barrier.oldLayout != barrier.newLayout) {
            cmd_buffer_.pipelineBarrier(srcInfo.stages, dstInfo.stages, {}, nullptr, nullptr, barrier);
        }
        presentTexture->setCurrentLayout(vk::ImageLayout::ePresentSrcKHR);

        swapchain_color_texture_ = nullptr;
    }
    rp_present_source_ = false;
}

}  // namespace mulan::engine
