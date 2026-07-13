/**
 * @file dx11_swap_chain.h
 * @brief D3D11 交换链实现
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/swap_chain.h"
#include "dx11_common.h"
#include "dx11_texture.h"
#include "../../rhi/window.h"

#include <memory>

namespace mulan::engine {

class DX11SwapChain final : public SwapChain {
public:
    DX11SwapChain(const SwapChainDesc& desc, ID3D11Device* device, IDXGIFactory2* factory, ID3D11DeviceContext* ctx,
                  const NativeWindowHandle& window);
    ~DX11SwapChain() = default;

    const SwapChainDesc& desc() const override { return m_desc; }
    bool isValid() const { return m_swapChain && m_backBufferTexture; }
    Texture* currentBackBuffer() override;
    Texture* depthTexture() override { return m_depthTexture ? m_depthTexture.get() : nullptr; }
    RenderPassBeginInfo renderPassBeginInfo() override;
    void present() override;
    void resize(uint32_t width, uint32_t height) override;

private:
    bool createBackBuffer();
    void releaseBackBuffer();

    SwapChainDesc m_desc;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_ctx;
    ComPtr<IDXGISwapChain1> m_swapChain;

    std::unique_ptr<DX11Texture> m_backBufferTexture;
    std::unique_ptr<DX11Texture> m_msaaColorTexture;
    std::unique_ptr<DX11Texture> m_depthTexture;
};

}  // namespace mulan::engine
