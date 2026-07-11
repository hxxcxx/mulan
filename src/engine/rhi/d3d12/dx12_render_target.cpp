#include "dx12_render_target.h"
#include "dx12_command_list.h"
#include "dx12_convert.h"

#include <mulan/core/result/error.h>
#include "../engine_error_code.h"
#include <mulan/core/log/log.h>

#include <stdexcept>
#include <string>

namespace mulan::engine {

core::Result<std::unique_ptr<DX12RenderTarget>> DX12RenderTarget::create(const RenderTargetDesc& desc,
                                                                         ID3D12Device* device) {
    auto obj = std::unique_ptr<DX12RenderTarget>(new DX12RenderTarget(desc, device));
    try {
        obj->createResources();
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::RenderTargetCreateFailed,
                                         std::string("DX12RenderTarget create failed: ") + e.what()));
    }
    return obj;
}

DX12RenderTarget::~DX12RenderTarget() = default;

void DX12RenderTarget::createResources() {
    const uint32_t samples = desc_.sampleCount > 1 ? desc_.sampleCount : 1;

    // Color texture
    auto colorDesc = TextureDesc::renderTarget(desc_.width, desc_.height, desc_.colorFormat);
    auto colorResult = DX12Texture::create(colorDesc, device_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (!colorResult)
        throw std::runtime_error(colorResult.error().message);
    color_texture_ = std::move(*colorResult);

    // RTV heap
    rtv_heap_ = std::make_unique<DX12DescriptorAllocator>(device_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                                          D3D12_DESCRIPTOR_HEAP_FLAG_NONE, samples > 1 ? 2 : 1);
    auto rtvDesc = rtv_heap_->allocate();
    device_->CreateRenderTargetView(color_texture_->resource(), nullptr, rtvDesc.cpu);
    rtv_handle_ = rtvDesc.cpu;
    color_texture_->setRTV(rtvDesc.cpu);

    if (samples > 1) {
        auto msaaColorDesc =
                TextureDesc::renderTarget(desc_.width, desc_.height, desc_.colorFormat, "OffscreenMSAAColor", samples);
        msaaColorDesc.usage = TextureUsageFlags::RenderTarget;
        auto msaaColorResult = DX12Texture::create(msaaColorDesc, device_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (!msaaColorResult)
            throw std::runtime_error(msaaColorResult.error().message);
        msaa_color_texture_ = std::move(*msaaColorResult);

        auto msaaRtvDesc = rtv_heap_->allocate();
        device_->CreateRenderTargetView(msaa_color_texture_->resource(), nullptr, msaaRtvDesc.cpu);
        msaa_color_texture_->setRTV(msaaRtvDesc.cpu);
    }

    if (desc_.hasDepth) {
        // Depth texture —— 纯 DSV 用途，不带 ShaderResource（见 DX12SwapChain 同样注释）
        auto depthDesc =
                TextureDesc::depthStencil(desc_.width, desc_.height, desc_.depthFormat, "OffscreenDepth", samples);
        depthDesc.usage = TextureUsageFlags::DepthStencil;  // 剥离 ShaderResource
        auto depthResult = DX12Texture::create(depthDesc, device_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if (!depthResult)
            throw std::runtime_error(depthResult.error().message);
        depth_texture_ = std::move(*depthResult);

        // DSV heap
        dsv_heap_ = std::make_unique<DX12DescriptorAllocator>(device_, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                                              D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 1);
        auto dsvDesc = dsv_heap_->allocate();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {};
        dsvViewDesc.Format = toDSVFormat(desc_.depthFormat);
        dsvViewDesc.ViewDimension = samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(depth_texture_->resource(), &dsvViewDesc, dsvDesc.cpu);
        dsv_handle_ = dsvDesc.cpu;
        depth_texture_->setDSV(dsvDesc.cpu);
    }
}

void DX12RenderTarget::resize(uint32_t width, uint32_t height) {
    desc_.width = width;
    desc_.height = height;
    color_texture_.reset();
    depth_texture_.reset();
    msaa_color_texture_.reset();
    rtv_heap_.reset();
    dsv_heap_.reset();
    // resize 是基类 void 热路径契约，内部消化错误。
    try {
        createResources();
    } catch (const std::exception& e) {}
}

RenderPassBeginInfo DX12RenderTarget::renderPassBeginInfo() {
    RenderPassBeginInfo info;
    Texture* color = msaa_color_texture_ ? static_cast<Texture*>(msaa_color_texture_.get()) : color_texture_.get();
    if (color) {
        info.colorAttachments[0].target = color;
        info.colorAttachments[0].resolveTarget = msaa_color_texture_ ? color_texture_.get() : nullptr;
        info.colorAttachments[0].loadAction = LoadAction::Clear;
        info.colorAttachments[0].storeAction = msaa_color_texture_ ? StoreAction::DontCare : StoreAction::Store;
        info.colorCount = 1;
    }
    if (depth_texture_) {
        info.depthAttachment.target = depth_texture_.get();
        info.depthAttachment.loadAction = LoadAction::Clear;
        info.depthAttachment.storeAction = StoreAction::DontCare;
    }
    auto& cc = desc_.clearColor;
    info.clearColor[0] = cc[0];
    info.clearColor[1] = cc[1];
    info.clearColor[2] = cc[2];
    info.clearColor[3] = cc[3];
    info.clearDepth = desc_.clearDepth;
    info.presentSource = false;
    info.width = desc_.width;
    info.height = desc_.height;
    info.nativeHandle = nativeRenderPassHandle();
    return info;
}

}  // namespace mulan::engine
