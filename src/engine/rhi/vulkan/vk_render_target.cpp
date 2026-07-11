#include "vk_render_target.h"

#include <mulan/core/result/error.h>
#include "../engine_error_code.h"

namespace mulan::engine {

core::Result<std::unique_ptr<VKRenderTarget>> VKRenderTarget::create(const RenderTargetDesc& desc, vk::Device device,
                                                                     VmaAllocator allocator) {
    auto obj = std::unique_ptr<VKRenderTarget>(new VKRenderTarget(desc, device, allocator));
    if (auto e = obj->createResources(); e.code != 0)
        return std::unexpected(e);
    return obj;
}

VKRenderTarget::~VKRenderTarget() {
    cleanup();
}

void VKRenderTarget::resize(uint32_t width, uint32_t height) {
    device_.waitIdle();
    cleanup();
    desc_.width = width;
    desc_.height = height;
    if (auto e = createResources(); e.code != 0) {}
}

core::Error VKRenderTarget::createResources() {
    const uint32_t samples = desc_.sampleCount > 1 ? desc_.sampleCount : 1;

    TextureDesc colorDesc = TextureDesc::renderTarget(desc_.width, desc_.height, desc_.colorFormat, "OffscreenColor");
    colorDesc.usage =
            TextureUsageFlags::RenderTarget | TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;

    auto colorResult = VKTexture::create(colorDesc, device_, allocator_);
    if (!colorResult)
        return colorResult.error();
    color_texture_ = std::move(*colorResult);

    if (samples > 1) {
        TextureDesc msaaColorDesc =
                TextureDesc::renderTarget(desc_.width, desc_.height, desc_.colorFormat, "OffscreenMSAAColor", samples);
        msaaColorDesc.usage = TextureUsageFlags::RenderTarget;
        auto msaaColorResult = VKTexture::create(msaaColorDesc, device_, allocator_);
        if (!msaaColorResult) {
            color_texture_.reset();
            return msaaColorResult.error();
        }
        msaa_color_texture_ = std::move(*msaaColorResult);
    }

    if (desc_.hasDepth) {
        auto depthDesc =
                TextureDesc::depthStencil(desc_.width, desc_.height, desc_.depthFormat, "OffscreenDepth", samples);
        auto depthResult = VKTexture::create(depthDesc, device_, allocator_);
        if (!depthResult) {
            msaa_color_texture_.reset();
            color_texture_.reset();
            return depthResult.error();
        }
        depth_texture_ = std::move(*depthResult);
    }

    return {};
}

void VKRenderTarget::cleanup() {
    msaa_color_texture_.reset();
    depth_texture_.reset();
    color_texture_.reset();
}

RenderPassBeginInfo VKRenderTarget::renderPassBeginInfo() {
    RenderPassBeginInfo info;
    Texture* color = msaa_color_texture_ ? static_cast<Texture*>(msaa_color_texture_.get()) : color_texture_.get();
    if (color) {
        info.colorAttachments[0].target = color;
        info.colorAttachments[0].resolveTarget = msaa_color_texture_ ? color_texture_.get() : nullptr;
        info.colorAttachments[0].loadAction = LoadAction::Clear;
        info.colorAttachments[0].storeAction = msaa_color_texture_ ? StoreAction::DontCare : StoreAction::Store;
        info.colorCount = 1;
    }
    if (depth_texture_) {
        info.depthAttachment.target = depth_texture_.get();
        info.depthAttachment.loadAction = LoadAction::Clear;
        info.depthAttachment.storeAction = StoreAction::DontCare;
    }

    auto& cc = desc_.clearColor;
    info.clearColor[0] = cc[0];
    info.clearColor[1] = cc[1];
    info.clearColor[2] = cc[2];
    info.clearColor[3] = cc[3];
    info.clearDepth = desc_.clearDepth;
    info.presentSource = false;
    info.width = desc_.width;
    info.height = desc_.height;
    info.nativeHandle = nativeRenderPassHandle();
    return info;
}

}  // namespace mulan::engine
