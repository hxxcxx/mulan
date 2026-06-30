/**
 * @file DX12SwapChain.cpp
 * @brief D3D12 交换链实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12SwapChain.h"
#include "DX12CommandList.h"
#include "DX12Convert.h"

namespace mulan::engine {

DX12SwapChain::DX12SwapChain(const SwapChainDesc& desc, ID3D12Device* device,
                             IDXGIFactory4* factory, ID3D12CommandQueue* queue,
                             const NativeWindowHandle& window)
    : m_desc(desc)
    , m_device(device)
    , m_queue(queue)
{
    m_clearColor[0] = 0.15f; m_clearColor[1] = 0.15f;
    m_clearColor[2] = 0.15f; m_clearColor[3] = 1.0f;

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width              = desc.width;
    scDesc.Height             = desc.height;
    scDesc.Format             = toDXGIFormat(desc.format);
    scDesc.Stereo             = FALSE;
    scDesc.SampleDesc.Count   = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount        = desc.bufferCount;
    scDesc.Scaling            = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
    scDesc.Flags              = desc.vsync ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    HWND hwnd = reinterpret_cast<HWND>(window.win32.hWnd);
    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = factory->CreateSwapChainForHwnd(
        queue, hwnd, &scDesc, nullptr, nullptr, &swapChain1);
    DX12_CHECK(hr);
    hr = swapChain1.As(&m_swapChain);
    DX12_CHECK(hr);
    createRTVHeap();
    createBackBuffers();
}

DX12SwapChain::~DX12SwapChain() {
    releaseBackBuffers();
}

void DX12SwapChain::createRTVHeap() {
    m_rtvHeap = std::make_unique<DX12DescriptorAllocator>(
        m_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE, m_desc.bufferCount);

    m_dsvHeap = std::make_unique<DX12DescriptorAllocator>(
        m_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 1);
}

void DX12SwapChain::createBackBuffers() {
    m_backBuffers.resize(m_desc.bufferCount);
    m_backBufferTextures.resize(m_desc.bufferCount);

    for (uint32_t i = 0; i < m_desc.bufferCount; ++i) {
        HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        DX12_CHECK(hr);
        auto rtvDesc = m_rtvHeap->allocate();
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtvDesc.cpu);

        // 包装 swapchain backbuffer，不重新创建资源
        TextureDesc texDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.format);
        m_backBufferTextures[i] = std::make_unique<DX12Texture>(
            texDesc, m_backBuffers[i].Get(), D3D12_RESOURCE_STATE_PRESENT);
        m_backBufferTextures[i]->setRTV(rtvDesc.cpu);
    }

    // Depth stencil —— 纯 DSV 用途，不带 ShaderResource：
    // 深度缓冲从不被 shader 采样，且带 SRV 会触发对 typed 深度格式
    // 创建非法 SRV，导致 device removed。
    auto depthDesc = TextureDesc::depthStencil(m_desc.width, m_desc.height);
    depthDesc.usage = TextureUsageFlags::DepthStencil;  // 剥离 ShaderResource
    m_depthTexture = std::make_unique<DX12Texture>(
        depthDesc, m_device, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    auto dsvDesc = m_dsvHeap->allocate();
    m_device->CreateDepthStencilView(m_depthTexture->resource(), nullptr, dsvDesc.cpu);
    m_depthTexture->setDSV(dsvDesc.cpu);
}

void DX12SwapChain::releaseBackBuffers() {
    m_backBufferTextures.clear();
    m_backBuffers.clear();
    m_depthTexture.reset();
    m_rtvHeap.reset();
    m_dsvHeap.reset();
}

Texture* DX12SwapChain::currentBackBuffer() {
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    if (m_frameIndex < m_backBufferTextures.size()) {
        return m_backBufferTextures[m_frameIndex].get();
    }
    return nullptr;
}

void DX12SwapChain::present() {
    UINT syncInterval = m_desc.vsync ? 1 : 0;
    UINT flags = m_desc.vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    HRESULT hr = m_swapChain->Present(syncInterval, flags);
    if (FAILED(hr)) {
        logDeviceRemovedReason(hr);
    }
    DX12_CHECK(hr);
}

void DX12SwapChain::logDeviceRemovedReason(HRESULT presentResult) const {
    HRESULT reason = m_device ? m_device->GetDeviceRemovedReason() : presentResult;
    DX12_LOG("[DX12] Present failed hr=0x%08X deviceRemovedReason=0x%08X\n",
             static_cast<unsigned>(presentResult), static_cast<unsigned>(reason));
    DX12_CHECK(reason);
}

void DX12SwapChain::resize(uint32_t width, uint32_t height) {
    releaseBackBuffers();

    m_desc.width  = width;
    m_desc.height = height;

    HRESULT hr = m_swapChain->ResizeBuffers(
        m_desc.bufferCount, width, height,
        toDXGIFormat(m_desc.format), 0);
    DX12_CHECK(hr);

    createRTVHeap();
    createBackBuffers();
}

} // namespace mulan::engine
