/**
 * @file dx12_render_target.h
 * @brief D3D12 离屏渲染目标实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../rhi/render_target.h"
#include "dx12_common.h"
#include "dx12_texture.h"
#include "dx12_descriptor_allocator.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class DX12RenderTarget final : public RenderTarget {
public:
    /// 创建 DX12RenderTarget。失败返回 RenderTargetCreateFailed。
    static Result<std::unique_ptr<DX12RenderTarget>> create(const RenderTargetDesc& desc, ID3D12Device* device);
    ~DX12RenderTarget();

    const RenderTargetDesc& desc() const override { return desc_; }
    Texture* colorTexture() override { return color_texture_.get(); }
    Texture* depthTexture() override { return depth_texture_.get(); }

    Result<void> resize(uint32_t width, uint32_t height) override;
    RenderPassBeginInfo renderPassBeginInfo() override;

private:
    DX12RenderTarget(const RenderTargetDesc& desc, ID3D12Device* device) : desc_(desc), device_(device) {}

    [[nodiscard]] Result<void> createResources();

    RenderTargetDesc desc_;
    ID3D12Device* device_;
    std::unique_ptr<DX12Texture> color_texture_;
    std::unique_ptr<DX12Texture> depth_texture_;
    std::unique_ptr<DX12Texture> msaa_color_texture_;

    std::unique_ptr<DX12DescriptorAllocator> rtv_heap_;
    std::unique_ptr<DX12DescriptorAllocator> dsv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle_ = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle_ = {};
};

}  // namespace mulan::engine
