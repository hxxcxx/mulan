#include "detail/dx12_sampler.h"
#include "detail/dx12_descriptor_allocator.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <string>

namespace mulan::engine {

// ============================================================
// Helper: RHI enum → D3D12 enum
// ============================================================

static D3D12_FILTER toDX12Filter(SamplerFilter min, SamplerFilter mag, SamplerFilter mip, bool anisotropy,
                                 bool comparison) {
    if (anisotropy) {
        return comparison ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
    }

    // 简化映射：枚举组合 → 具体 D3D12_FILTER
    if (min == SamplerFilter::Linear && mag == SamplerFilter::Linear && mip == SamplerFilter::Linear)
        return comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    if (min == SamplerFilter::Linear && mag == SamplerFilter::Linear && mip == SamplerFilter::Nearest)
        return comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Nearest && mip == SamplerFilter::Nearest)
        return comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Nearest && mip == SamplerFilter::Linear)
        return comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR : D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    if (min == SamplerFilter::Linear && mag == SamplerFilter::Nearest && mip == SamplerFilter::Nearest)
        return comparison ? D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT : D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Linear && mip == SamplerFilter::Nearest)
        return comparison ? D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT
                          : D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;

    return comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
}

static D3D12_TEXTURE_ADDRESS_MODE toDX12AddressMode(SamplerAddressMode m) {
    switch (m) {
    case SamplerAddressMode::Repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case SamplerAddressMode::MirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case SamplerAddressMode::ClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case SamplerAddressMode::ClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case SamplerAddressMode::MirrorClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

static D3D12_COMPARISON_FUNC toDX12ComparisonFunc(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never: return D3D12_COMPARISON_FUNC_NEVER;
    case CompareFunc::Less: return D3D12_COMPARISON_FUNC_LESS;
    case CompareFunc::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareFunc::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareFunc::Greater: return D3D12_COMPARISON_FUNC_GREATER;
    case CompareFunc::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareFunc::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_NEVER;
}

// ============================================================
// DX12Sampler
// ============================================================

core::Result<std::unique_ptr<DX12Sampler>> DX12Sampler::create(const SamplerDesc& desc, ID3D12Device* device,
                                                               DX12DescriptorAllocator* samplerHeap) {
    // samplerHeap 为空时保留空 descriptor，供不需要 shader 绑定的调用方使用；
    // 正常 DX12Device 路径传入 shader-visible sampler heap。
    if (!device)
        return std::unexpected(makeError(EngineErrorCode::SamplerCreateFailed, "DX12Sampler requires a device"));
    auto sampler = std::unique_ptr<DX12Sampler>(new DX12Sampler(desc, device, samplerHeap));
    if (samplerHeap && !sampler->gpuHandle().ptr) {
        return std::unexpected(
                makeError(EngineErrorCode::SamplerCreateFailed, "DX12 shader-visible sampler heap is exhausted"));
    }
    return sampler;
}

DX12Sampler::DX12Sampler(const SamplerDesc& desc, ID3D12Device* device, DX12DescriptorAllocator* samplerHeap)
    : desc_(desc) {
    D3D12_SAMPLER_DESC d3dDesc = {};
    d3dDesc.Filter =
            toDX12Filter(desc.minFilter, desc.magFilter, desc.mipFilter, desc.anisotropyEnable, desc.compareEnable);
    d3dDesc.AddressU = toDX12AddressMode(desc.addressU);
    d3dDesc.AddressV = toDX12AddressMode(desc.addressV);
    d3dDesc.AddressW = toDX12AddressMode(desc.addressW);
    d3dDesc.MipLODBias = desc.mipLodBias;
    d3dDesc.MaxAnisotropy = desc.anisotropyEnable ? static_cast<UINT>(desc.maxAniso) : 16u;
    d3dDesc.ComparisonFunc = desc.compareEnable ? toDX12ComparisonFunc(desc.compareFunc) : D3D12_COMPARISON_FUNC_NEVER;
    d3dDesc.BorderColor[0] = desc.borderColor[0];
    d3dDesc.BorderColor[1] = desc.borderColor[1];
    d3dDesc.BorderColor[2] = desc.borderColor[2];
    d3dDesc.BorderColor[3] = desc.borderColor[3];
    d3dDesc.MinLOD = desc.minLod;
    d3dDesc.MaxLOD = desc.maxLod;

    if (samplerHeap) {
        descriptor_ = samplerHeap->allocate();
        if (descriptor_.cpu.ptr)
            device->CreateSampler(&d3dDesc, descriptor_.cpu);
    }
    // samplerHeap 为空时 descriptor_ 保持零值；正常绘制路径会使用其 GPU handle。
}

DX12Sampler::~DX12Sampler() {
    // 描述符由 DX12DescriptorAllocator 统一管理，无需单独释放
}

}  // namespace mulan::engine
