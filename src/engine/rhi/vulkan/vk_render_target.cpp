#include "vk_render_target.h"

#include <mulan/core/result/error.h>
#include "../../engine_error_code.h"
#include <mulan/core/log/log.h>

#include <string>

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
    // resize 是基类 void 热路径契约，内部消化错误。
    if (auto e = createResources(); e.code != 0) {}
}

// ============================================================
// 内部资源创建
// ============================================================

core::Error VKRenderTarget::createResources() {
    // --- Color 纹理 ---
    TextureDesc colorDesc;
    colorDesc.name = "OffscreenColor";
    colorDesc.format = desc_.colorFormat;
    colorDesc.dimension = TextureDimension::Texture2D;
    colorDesc.usage =
            TextureUsageFlags::RenderTarget | TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    colorDesc.width = desc_.width;
    colorDesc.height = desc_.height;

    auto colorResult = VKTexture::create(colorDesc, device_, allocator_);
    if (!colorResult)
        return colorResult.error();
    color_texture_ = std::move(*colorResult);

    // --- Depth 纹理 ---
    if (desc_.hasDepth) {
        auto depthDesc = TextureDesc::depthStencil(desc_.width, desc_.height, desc_.depthFormat, "OffscreenDepth");
        auto depthResult = VKTexture::create(depthDesc, device_, allocator_);
        if (!depthResult) {
            color_texture_.reset();
            return depthResult.error();
        }
        depth_texture_ = std::move(*depthResult);
    }

    return {};
}

void VKRenderTarget::cleanup() {
    depth_texture_.reset();
    color_texture_.reset();
}

}  // namespace mulan::engine
