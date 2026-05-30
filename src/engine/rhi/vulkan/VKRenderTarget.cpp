/**
 * @file VKRenderTarget.cpp
 * @brief Vulkan 离屏渲染目标实现
 * @author hxxcxx
 * @date 2026-04-17
 */

#include "VKRenderTarget.h"

namespace mulan::engine {

VKRenderTarget::VKRenderTarget(const RenderTargetDesc& desc,
                               vk::Device device, VmaAllocator allocator)
    : m_desc(desc), m_device(device), m_allocator(allocator)
{
    createResources();
}

VKRenderTarget::~VKRenderTarget() {
    cleanup();
}

void VKRenderTarget::resize(uint32_t width, uint32_t height) {
    m_device.waitIdle();
    cleanup();
    m_desc.width  = width;
    m_desc.height = height;
    createResources();
}

// ============================================================
// 内部资源创建
// ============================================================

void VKRenderTarget::createResources() {
    // --- Color 纹理 ---
    // GenerateMips 标志使 VKTexture 添加 eTransferSrc | eTransferDst，支持后续回读
    TextureDesc colorDesc;
    colorDesc.name      = "OffscreenColor";
    colorDesc.format    = m_desc.colorFormat;
    colorDesc.dimension = TextureDimension::Texture2D;
    colorDesc.usage     = TextureUsageFlags::RenderTarget
                        | TextureUsageFlags::ShaderResource
                        | TextureUsageFlags::GenerateMips;
    colorDesc.width     = m_desc.width;
    colorDesc.height    = m_desc.height;

    m_colorTexture = std::make_unique<VKTexture>(colorDesc, m_device, m_allocator);

    // --- Depth 纹理 ---
    if (m_desc.hasDepth) {
        auto depthDesc = TextureDesc::depthStencil(
            m_desc.width, m_desc.height, m_desc.depthFormat, "OffscreenDepth");
        m_depthTexture = std::make_unique<VKTexture>(depthDesc, m_device, m_allocator);
    }

    // --- Render Pass ---
    std::vector<vk::AttachmentDescription> attachments;

    vk::AttachmentDescription colorAttachment;
    colorAttachment.format         = toVkFormat(m_desc.colorFormat);
    colorAttachment.samples        = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp         = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp        = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout  = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;
    attachments.push_back(colorAttachment);

    vk::AttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout     = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint    = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    vk::AttachmentReference depthRef;
    if (m_desc.hasDepth) {
        vk::AttachmentDescription depthAttachment;
        depthAttachment.format         = toVkFormat(m_desc.depthFormat);
        depthAttachment.samples        = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp         = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp        = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout  = vk::ImageLayout::eUndefined;
        depthAttachment.finalLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        attachments.push_back(depthAttachment);

        depthRef.attachment = 1;
        depthRef.layout     = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        subpass.pDepthStencilAttachment = &depthRef;
    }

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

    m_renderPass = m_device.createRenderPass(rpCI);

    // --- Framebuffer ---
    std::vector<vk::ImageView> fbAttachments;
    fbAttachments.push_back(m_colorTexture->view());
    if (m_depthTexture) {
        fbAttachments.push_back(m_depthTexture->view());
    }

    vk::FramebufferCreateInfo fbCI;
    fbCI.renderPass      = m_renderPass;
    fbCI.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
    fbCI.pAttachments    = fbAttachments.data();
    fbCI.width           = m_desc.width;
    fbCI.height          = m_desc.height;
    fbCI.layers          = 1;

    m_framebuffer = m_device.createFramebuffer(fbCI);
}

void VKRenderTarget::cleanup() {
    if (m_framebuffer) {
        m_device.destroyFramebuffer(m_framebuffer);
        m_framebuffer = nullptr;
    }

    m_depthTexture.reset();
    m_colorTexture.reset();

    if (m_renderPass) {
        m_device.destroyRenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
}

} // namespace mulan::engine
