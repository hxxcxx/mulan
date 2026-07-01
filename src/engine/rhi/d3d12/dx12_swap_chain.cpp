#include "dx12_swap_chain.h"
#include "dx12_command_list.h"
#include "dx12_convert.h"

namespace mulan::engine {

DX12SwapChain::DX12SwapChain(const SwapChainDesc& desc, ID3D12Device* device,
                             IDXGIFactory4* factory, ID3D12CommandQueue* queue,
                             const NativeWindowHandle& window)
    : desc_(desc)
    , device_(device)
    , queue_(queue)
{
    clear_color_[0] = 0.15f; clear_color_[1] = 0.15f;
    clear_color_[2] = 0.15f; clear_color_[3] = 1.0f;

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
    hr = swapChain1.As(&swap_chain_);
    DX12_CHECK(hr);
    createRTVHeap();
    createBackBuffers();
}

DX12SwapChain::~DX12SwapChain() {
    releaseBackBuffers();
}

void DX12SwapChain::createRTVHeap() {
    rtv_heap_ = std::make_unique<DX12DescriptorAllocator>(
        device_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE, desc_.bufferCount);

    dsv_heap_ = std::make_unique<DX12DescriptorAllocator>(
        device_, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 1);
}

void DX12SwapChain::createBackBuffers() {
    back_buffers_.resize(desc_.bufferCount);
    back_buffer_textures_.resize(desc_.bufferCount);

    for (uint32_t i = 0; i < desc_.bufferCount; ++i) {
        HRESULT hr = swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffers_[i]));
        DX12_CHECK(hr);
        auto rtvDesc = rtv_heap_->allocate();
        device_->CreateRenderTargetView(back_buffers_[i].Get(), nullptr, rtvDesc.cpu);

        // 包装 swapchain backbuffer，不重新创建资源
        TextureDesc texDesc = TextureDesc::renderTarget(desc_.width, desc_.height, desc_.format);
        back_buffer_textures_[i] = std::make_unique<DX12Texture>(
            texDesc, back_buffers_[i].Get(), D3D12_RESOURCE_STATE_PRESENT);
        back_buffer_textures_[i]->setRTV(rtvDesc.cpu);
    }

    // Depth stencil —— 纯 DSV 用途，不带 ShaderResource：
    // 深度缓冲从不被 shader 采样，且带 SRV 会触发对 typed 深度格式
    // 创建非法 SRV，导致 device removed。
    auto depthDesc = TextureDesc::depthStencil(desc_.width, desc_.height);
    depthDesc.usage = TextureUsageFlags::DepthStencil;  // 剥离 ShaderResource
    depth_texture_ = std::make_unique<DX12Texture>(
        depthDesc, device_, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    auto dsvDesc = dsv_heap_->allocate();
    device_->CreateDepthStencilView(depth_texture_->resource(), nullptr, dsvDesc.cpu);
    depth_texture_->setDSV(dsvDesc.cpu);
}

void DX12SwapChain::releaseBackBuffers() {
    back_buffer_textures_.clear();
    back_buffers_.clear();
    depth_texture_.reset();
    rtv_heap_.reset();
    dsv_heap_.reset();
}

Texture* DX12SwapChain::currentBackBuffer() {
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    if (frame_index_ < back_buffer_textures_.size()) {
        return back_buffer_textures_[frame_index_].get();
    }
    return nullptr;
}

void DX12SwapChain::present() {
    UINT syncInterval = desc_.vsync ? 1 : 0;
    UINT flags = desc_.vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    HRESULT hr = swap_chain_->Present(syncInterval, flags);
    if (FAILED(hr)) {
        logDeviceRemovedReason(hr);
    }
    DX12_CHECK(hr);
}

void DX12SwapChain::logDeviceRemovedReason(HRESULT presentResult) const {
    HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : presentResult;
    DX12_LOG("[DX12] Present failed hr=0x%08X deviceRemovedReason=0x%08X\n",
             static_cast<unsigned>(presentResult), static_cast<unsigned>(reason));
    DX12_CHECK(reason);
}

void DX12SwapChain::resize(uint32_t width, uint32_t height) {
    releaseBackBuffers();

    desc_.width  = width;
    desc_.height = height;

    HRESULT hr = swap_chain_->ResizeBuffers(
        desc_.bufferCount, width, height,
        toDXGIFormat(desc_.format), 0);
    DX12_CHECK(hr);

    createRTVHeap();
    createBackBuffers();
}

} // namespace mulan::engine
