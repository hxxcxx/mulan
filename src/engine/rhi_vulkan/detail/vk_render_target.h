/**
 * @file vk_render_target.h
 * @brief Vulkan 离屏渲染目标实现
 * @author hxxcxx
 * @date 2026-04-17
 */

#pragma once

#include "../rhi/render_target.h"
#include "vk_convert.h"
#include "vk_texture.h"
#include "vk_command_list.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class VKRenderTarget : public RenderTarget {
public:
    /// 创建 VKRenderTarget。失败返回 RenderTargetCreateFailed。
    static core::Result<std::unique_ptr<VKRenderTarget>> create(const RenderTargetDesc& desc, vk::Device device,
                                                                VmaAllocator allocator);
    ~VKRenderTarget();

    const RenderTargetDesc& desc() const override { return desc_; }

    Texture* colorTexture() override { return color_texture_.get(); }
    Texture* depthTexture() override { return depth_texture_.get(); }

    core::Result<void> resize(uint32_t width, uint32_t height) override;
    RenderPassBeginInfo renderPassBeginInfo() override;

private:
    VKRenderTarget(const RenderTargetDesc& desc, vk::Device device, VmaAllocator allocator)
        : desc_(desc), device_(device), allocator_(allocator) {}

    /// 创建/重建资源。成功返回默认 Error(code=0)。
    core::Error createResources();
    void cleanup();

    RenderTargetDesc desc_;
    vk::Device device_;
    VmaAllocator allocator_;

    std::unique_ptr<VKTexture> color_texture_;
    std::unique_ptr<VKTexture> depth_texture_;
    std::unique_ptr<VKTexture> msaa_color_texture_;
};

}  // namespace mulan::engine
