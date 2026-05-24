/**
 * @file DX11SwapChain.cpp
 * @brief D3D11 交换链实现
 * @author zmb
 * @date 2026-04-19
 */
#include "DX11SwapChain.h"
#include "DX11CommandList.h"
#include "DX11Convert.h"
#include <cstdio>

namespace mulan::engine
{

DX11SwapChain::DX11SwapChain(const SwapChainDesc& desc, ID3D11Device* device,
                             IDXGIFactory2* factory, ID3D11DeviceContext* ctx,
                             const NativeWindowHandle& window,
                             const RenderConfig& renderConfig)
    : m_desc(desc)
    , m_device(device)
    , m_ctx(ctx)
    , m_renderConfig(renderConfig)
{
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width              = desc.width;
    scDesc.Height             = desc.height;
    scDesc.Format             = toDXGIFormat11(desc.format);
    scDesc.Stereo             = FALSE;
    scDesc.SampleDesc.Count   = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount        = desc.bufferCount;
    scDesc.Scaling            = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
    scDesc.Flags              = 0;

    HWND hwnd = reinterpret_cast<HWND>(window.win32.hWnd);
    HRESULT hr = factory->CreateSwapChainForHwnd(
        device, hwnd, &scDesc, nullptr, nullptr, &m_swapChain);
    DX11_CHECK(hr);

    createBackBuffer();
}

void DX11SwapChain::createBackBuffer()
{
    // Get back buffer texture
    ComPtr<ID3D11Texture2D> backBuf;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuf));
    DX11_CHECK(hr);

    TextureDesc texDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.format);
    m_backBufferTexture = std::make_unique<DX11Texture>(texDesc, backBuf.Get());
    m_backBufferTexture->createRTV(m_device);

    // Depth stencil
    auto depthDesc = TextureDesc::depthStencil(m_desc.width, m_desc.height);
    m_depthTexture = std::make_unique<DX11Texture>(depthDesc, m_device);
}

void DX11SwapChain::releaseBackBuffer()
{
    // Unbind render targets first
    m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    m_backBufferTexture.reset();
    m_depthTexture.reset();
}

Texture* DX11SwapChain::currentBackBuffer()
{
    return m_backBufferTexture.get();
}

void DX11SwapChain::present()
{
    UINT syncInterval = m_desc.vsync ? 1 : 0;
    m_swapChain->Present(syncInterval, 0);
}

void DX11SwapChain::resize(uint32_t width, uint32_t height)
{
    releaseBackBuffer();

    m_desc.width  = width;
    m_desc.height = height;

    HRESULT hr = m_swapChain->ResizeBuffers(
        m_desc.bufferCount, width, height,
        toDXGIFormat11(m_desc.format), 0);
    DX11_CHECK(hr);

    createBackBuffer();
}

} // namespace mulan::Engine
