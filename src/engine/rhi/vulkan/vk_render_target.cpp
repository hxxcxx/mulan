#include "vk_render_target.h"

namespace mulan::engine {

VKRenderTarget::VKRenderTarget(const RenderTargetDesc& desc,
                               vk::Device device, VmaAllocator allocator)
    : desc_(desc), device_(device), allocator_(allocator)
{
    createResources();
}

VKRenderTarget::~VKRenderTarget() {
    cleanup();
}

void VKRenderTarget::resize(uint32_t width, uint32_t height) {
    device_.waitIdle();
    cleanup();
    desc_.width  = width;
    desc_.height = height;
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
    colorDesc.format    = desc_.colorFormat;
    colorDesc.dimension = TextureDimension::Texture2D;
    colorDesc.usage     = TextureUsageFlags::RenderTarget
                        | TextureUsageFlags::ShaderResource
                        | TextureUsageFlags::GenerateMips;
    colorDesc.width     = desc_.width;
    colorDesc.height    = desc_.height;

    color_texture_ = std::make_unique<VKTexture>(colorDesc, device_, allocator_);

    // --- Depth 纹理 ---
    if (desc_.hasDepth) {
        auto depthDesc = TextureDesc::depthStencil(
            desc_.width, desc_.height, desc_.depthFormat, "OffscreenDepth");
        depth_texture_ = std::make_unique<VKTexture>(depthDesc, device_, allocator_);
    }

    // --- Render Pass ---
    std::vector<vk::AttachmentDescription> attachments;

    vk::AttachmentDescription colorAttachment;
    colorAttachment.format         = toVkFormat(desc_.colorFormat);
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
    if (desc_.hasDepth) {
        vk::AttachmentDescription depthAttachment;
        depthAttachment.format         = toVkFormat(desc_.depthFormat);
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

    render_pass_ = device_.createRenderPass(rpCI);

    // --- Framebuffer ---
    std::vector<vk::ImageView> fbAttachments;
    fbAttachments.push_back(color_texture_->view());
    if (depth_texture_) {
        fbAttachments.push_back(depth_texture_->view());
    }

    vk::FramebufferCreateInfo fbCI;
    fbCI.renderPass      = render_pass_;
    fbCI.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
    fbCI.pAttachments    = fbAttachments.data();
    fbCI.width           = desc_.width;
    fbCI.height          = desc_.height;
    fbCI.layers          = 1;

    framebuffer_ = device_.createFramebuffer(fbCI);
}

void VKRenderTarget::cleanup() {
    if (framebuffer_) {
        device_.destroyFramebuffer(framebuffer_);
        framebuffer_ = nullptr;
    }

    depth_texture_.reset();
    color_texture_.reset();

    if (render_pass_) {
        device_.destroyRenderPass(render_pass_);
        render_pass_ = nullptr;
    }
}

} // namespace mulan::engine
