/**
 * @file dx12_swap_chain.h
 * @brief D3D12 交换链实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../rhi/swap_chain.h"
#include "dx12_common.h"
#include "dx12_convert.h"
#include "dx12_texture.h"
#include "dx12_descriptor_allocator.h"
#include "../rhi/window.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>
#include <vector>
#include <array>

namespace mulan::engine {

class DX12SwapChain final : public SwapChain {
public:
    /// 创建 DX12SwapChain。失败返回 SwapChainCreateFailed。
    static core::Result<std::unique_ptr<DX12SwapChain>> create(const SwapChainDesc& desc, ID3D12Device* device,
                                                               IDXGIFactory4* factory, ID3D12CommandQueue* queue,
                                                               const NativeWindowHandle& window);
    ~DX12SwapChain();

    const SwapChainDesc& desc() const override { return desc_; }
    Texture* currentBackBuffer() override;
    Texture* depthTexture() override { return depth_texture_ ? depth_texture_.get() : nullptr; }
    RenderPassBeginInfo renderPassBeginInfo() override;
    void present() override;
    void resize(uint32_t width, uint32_t height) override;

    uint32_t currentFrameIndex() const { return frame_index_; }
    DXGI_FORMAT rtvFormat() const { return toDXGIFormat(desc_.format); }

private:
    DX12SwapChain(const SwapChainDesc& desc, ID3D12Device* device, IDXGIFactory4* factory, ID3D12CommandQueue* queue,
                  const NativeWindowHandle& window);

    void createRTVHeap();
    void createBackBuffers();
    void createMsaaColor();
    void releaseBackBuffers();
    void logDeviceRemovedReason(HRESULT presentResult) const;

    SwapChainDesc desc_;
    ID3D12Device* device_;
    ID3D12CommandQueue* queue_;
    ComPtr<IDXGISwapChain3> swap_chain_;

    std::unique_ptr<DX12DescriptorAllocator> rtv_heap_;
    std::vector<ComPtr<ID3D12Resource>> back_buffers_;
    std::vector<std::unique_ptr<DX12Texture>> back_buffer_textures_;
    std::unique_ptr<DX12Texture> msaa_color_texture_;

    // DS
    std::unique_ptr<DX12Texture> depth_texture_;
    std::unique_ptr<DX12DescriptorAllocator> dsv_heap_;

    uint32_t frame_index_ = 0;
    float clear_color_[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
};

}  // namespace mulan::engine
