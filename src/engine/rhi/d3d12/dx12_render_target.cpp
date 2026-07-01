#include "dx12_render_target.h"
#include "dx12_command_list.h"
#include "dx12_convert.h"

namespace mulan::engine {

DX12RenderTarget::DX12RenderTarget(const RenderTargetDesc& desc,
                                   ID3D12Device* device)
    : desc_(desc)
    , device_(device)
{
    createResources();
}

DX12RenderTarget::~DX12RenderTarget() = default;

void DX12RenderTarget::createResources() {
    // Color texture
    auto colorDesc = TextureDesc::renderTarget(desc_.width, desc_.height, desc_.colorFormat);
    color_texture_ = std::make_unique<DX12Texture>(
        colorDesc, device_, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // RTV heap
    rtv_heap_ = std::make_unique<DX12DescriptorAllocator>(
        device_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 1);
    auto rtvDesc = rtv_heap_->allocate();
    device_->CreateRenderTargetView(
        color_texture_->resource(), nullptr, rtvDesc.cpu);
    rtv_handle_ = rtvDesc.cpu;
    color_texture_->setRTV(rtvDesc.cpu);

    if (desc_.hasDepth) {
        // Depth texture —— 纯 DSV 用途，不带 ShaderResource（见 DX12SwapChain 同样注释）
        auto depthDesc = TextureDesc::depthStencil(desc_.width, desc_.height, desc_.depthFormat);
        depthDesc.usage = TextureUsageFlags::DepthStencil;  // 剥离 ShaderResource
        depth_texture_ = std::make_unique<DX12Texture>(
            depthDesc, device_, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        // DSV heap
        dsv_heap_ = std::make_unique<DX12DescriptorAllocator>(
            device_, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 1);
        auto dsvDesc = dsv_heap_->allocate();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {};
        dsvViewDesc.Format        = toDSVFormat(desc_.depthFormat);
        dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(
            depth_texture_->resource(), &dsvViewDesc, dsvDesc.cpu);
        dsv_handle_ = dsvDesc.cpu;
        depth_texture_->setDSV(dsvDesc.cpu);
    }
}

void DX12RenderTarget::resize(uint32_t width, uint32_t height) {
    desc_.width  = width;
    desc_.height = height;
    color_texture_.reset();
    depth_texture_.reset();
    rtv_heap_.reset();
    dsv_heap_.reset();
    createResources();
}

} // namespace mulan::engine
