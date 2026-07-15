#include "detail/dx11_swap_chain.h"
#include "detail/dx11_convert.h"
#include "../rhi/engine_error_code.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace mulan::engine {

DX11SwapChain::DX11SwapChain(const SwapChainDesc& desc, ID3D11Device* device, IDXGIFactory2* factory,
                             ID3D11DeviceContext* ctx, const NativeWindowHandle& window)
    : m_desc(desc), m_device(device), m_ctx(ctx) {
    if (!m_device || !factory || !m_ctx || window.type != NativeWindowHandle::Type::Win32 || !window.valid() ||
        m_desc.width == 0 || m_desc.height == 0 || m_desc.bufferCount < 2) {
        LOG_ERROR("[DX11] Swap chain initialization rejected: invalid arguments");
        return;
    }

    m_desc.sampleCount = m_desc.sampleCount > 1 ? m_desc.sampleCount : 1;
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = desc.width;
    scDesc.Height = desc.height;
    scDesc.Format = toDXGIFormat11(desc.format);
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = desc.bufferCount;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scDesc.Flags = 0;

    HWND hwnd = reinterpret_cast<HWND>(window.win32.hWnd);
    if (!checkDX11(factory->CreateSwapChainForHwnd(device, hwnd, &scDesc, nullptr, nullptr, &m_swapChain),
                   "IDXGIFactory2::CreateSwapChainForHwnd"))
        return;

    createBackBuffer();
}

bool DX11SwapChain::createBackBuffer() {
    if (!m_swapChain)
        return false;

    ComPtr<ID3D11Texture2D> backBuf;
    if (!checkDX11(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuf)), "IDXGISwapChain1::GetBuffer"))
        return false;

    TextureDesc texDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.format, "DX11BackBuffer");
    texDesc.usage = TextureUsageFlags::RenderTarget;
    auto backBufferTexture = std::make_unique<DX11Texture>(texDesc, backBuf.Get());
    backBufferTexture->createRTV(m_device);
    if (!backBufferTexture->isValid())
        return false;

    std::unique_ptr<DX11Texture> msaaColorTexture;
    if (m_desc.sampleCount > 1) {
        auto msaaDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.format, "DX11SwapChainMSAAColor",
                                                  m_desc.sampleCount);
        msaaDesc.usage = TextureUsageFlags::RenderTarget;
        msaaColorTexture = std::make_unique<DX11Texture>(msaaDesc, m_device);
        if (!msaaColorTexture->isValid())
            return false;
    }

    std::unique_ptr<DX11Texture> depthTexture;
    if (m_desc.hasDepth) {
        auto depthDesc = TextureDesc::depthStencil(m_desc.width, m_desc.height, m_desc.depthFormat,
                                                   "DX11SwapChainDepth", m_desc.sampleCount);
        depthDesc.usage = TextureUsageFlags::DepthStencil;
        depthTexture = std::make_unique<DX11Texture>(depthDesc, m_device);
        if (!depthTexture->isValid())
            return false;
    }

    m_backBufferTexture = std::move(backBufferTexture);
    m_msaaColorTexture = std::move(msaaColorTexture);
    m_depthTexture = std::move(depthTexture);
    return true;
}

void DX11SwapChain::releaseBackBuffer() {
    // Unbind render targets first
    m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    m_backBufferTexture.reset();
    m_msaaColorTexture.reset();
    m_depthTexture.reset();
}

Texture* DX11SwapChain::currentBackBuffer() {
    return m_backBufferTexture.get();
}

RenderPassBeginInfo DX11SwapChain::renderPassBeginInfo() {
    RenderPassBeginInfo info;
    Texture* color = m_msaaColorTexture ? static_cast<Texture*>(m_msaaColorTexture.get()) : currentBackBuffer();
    if (color) {
        info.colorAttachments[0].target = color;
        info.colorAttachments[0].resolveTarget = m_msaaColorTexture ? currentBackBuffer() : nullptr;
        info.colorAttachments[0].loadAction = LoadAction::Clear;
        info.colorAttachments[0].storeAction = m_msaaColorTexture ? StoreAction::DontCare : StoreAction::Store;
        info.colorCount = 1;
    }
    if (m_depthTexture) {
        info.depthAttachment.target = m_depthTexture.get();
        info.depthAttachment.loadAction = LoadAction::Clear;
        info.depthAttachment.storeAction = StoreAction::DontCare;
    }
    std::copy(std::begin(m_desc.clearColor), std::end(m_desc.clearColor), info.clearColor);
    info.clearDepth = m_desc.clearDepth;
    info.presentSource = true;
    info.width = m_desc.width;
    info.height = m_desc.height;
    return info;
}

ResultVoid DX11SwapChain::present() {
    UINT syncInterval = m_desc.vsync ? 1 : 0;
    HRESULT hr = m_swapChain->Present(syncInterval, 0);
    if (FAILED(hr)) {
        logDX11Failure(hr, "IDXGISwapChain::Present");
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            logDX11Failure(m_device->GetDeviceRemovedReason(), "ID3D11Device::GetDeviceRemovedReason");
        return std::unexpected(makeError(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET
                                                 ? EngineErrorCode::DeviceLost
                                                 : EngineErrorCode::PresentationFailed,
                                         "DX11 swapchain presentation failed"));
    }
    return {};
}

ResultVoid DX11SwapChain::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "DX11 swapchain size must be non-zero"));

    releaseBackBuffer();
    // ResizeBuffers 要求 context 不再保存旧 backbuffer 的任何引用。
    m_ctx->ClearState();
    m_ctx->Flush();

    HRESULT hr = m_swapChain->ResizeBuffers(m_desc.bufferCount, width, height, toDXGIFormat11(m_desc.format), 0);
    if (FAILED(hr)) {
        logDX11Failure(hr, "IDXGISwapChain::ResizeBuffers");
        // Resize 失败时 DXGI 仍保留原 backbuffer；重新包装它，避免交换链对象在
        // 下一帧只因一次临时失败而永久失效。
        if (!createBackBuffer())
            LOG_ERROR("[DX11] Swap chain backbuffer restore failed");
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "DX11 swapchain ResizeBuffers failed"));
    }

    m_desc.width = width;
    m_desc.height = height;

    if (!createBackBuffer())
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "DX11 swapchain backbuffer recreation failed"));
    return {};
}

}  // namespace mulan::engine
