/**
 * @file DX12SwapChain.h
 * @brief D3D12 交换链实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../SwapChain.h"
#include "DX12Common.h"
#include "DX12Convert.h"
#include "DX12Texture.h"
#include "DX12DescriptorAllocator.h"
#include "../../Window.h"

#include <vector>
#include <memory>
#include <array>

namespace mulan::engine {

class DX12SwapChain final : public SwapChain {
public:
    DX12SwapChain(const SwapChainDesc& desc, ID3D12Device* device,
                  IDXGIFactory4* factory, ID3D12CommandQueue* queue,
                  const NativeWindowHandle& window);
    ~DX12SwapChain();

    const SwapChainDesc& desc() const override { return m_desc; }
    Texture* currentBackBuffer() override;
    Texture* depthTexture() override {
        return m_depthTexture ? m_depthTexture.get() : nullptr;
    }
    void present() override;
    void resize(uint32_t width, uint32_t height) override;

    uint32_t currentFrameIndex() const { return m_frameIndex; }
    DXGI_FORMAT rtvFormat() const { return toDXGIFormat(m_desc.format); }

private:
    void createRTVHeap();
    void createBackBuffers();
    void releaseBackBuffers();
    void logDeviceRemovedReason(HRESULT presentResult) const;

    SwapChainDesc                       m_desc;
    ID3D12Device*                       m_device;
    ID3D12CommandQueue*                 m_queue;
    ComPtr<IDXGISwapChain3>             m_swapChain;

    std::unique_ptr<DX12DescriptorAllocator> m_rtvHeap;
    std::vector<ComPtr<ID3D12Resource>>      m_backBuffers;
    std::vector<std::unique_ptr<DX12Texture>> m_backBufferTextures;

    // DS
    std::unique_ptr<DX12Texture>       m_depthTexture;
    std::unique_ptr<DX12DescriptorAllocator> m_dsvHeap;

    uint32_t                           m_frameIndex = 0;
    float                              m_clearColor[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
};

} // namespace mulan::engine
