/**
 * @file DX11SwapChain.h
 * @brief D3D11 交换链实现
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../SwapChain.h"
#include "DX11Common.h"
#include "DX11Texture.h"
#include "../../Window.h"

#include <memory>

namespace mulan::engine
{

class DX11SwapChain final : public SwapChain
{
public:
    DX11SwapChain(const SwapChainDesc& desc, ID3D11Device* device,
                  IDXGIFactory2* factory, ID3D11DeviceContext* ctx,
                  const NativeWindowHandle& window,
                  const RenderConfig& renderConfig);
    ~DX11SwapChain() = default;

    const SwapChainDesc& desc() const override { return m_desc; }
    Texture* currentBackBuffer() override;
    Texture* depthTexture() override {
        return m_depthTexture ? m_depthTexture.get() : nullptr;
    }
    void present() override;
    void resize(uint32_t width, uint32_t height) override;

private:
    void createBackBuffer();
    void releaseBackBuffer();

    SwapChainDesc                       m_desc;
    ID3D11Device*                       m_device;
    ID3D11DeviceContext*                m_ctx;
    ComPtr<IDXGISwapChain1>             m_swapChain;

    std::unique_ptr<DX11Texture>        m_backBufferTexture;
    std::unique_ptr<DX11Texture>        m_depthTexture;

    RenderConfig                        m_renderConfig;
};

} // namespace mulan::engine
