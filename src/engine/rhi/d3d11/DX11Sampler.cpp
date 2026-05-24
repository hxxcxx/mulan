/**
 * @file DX11Sampler.cpp
 * @brief D3D11 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#include "DX11Sampler.h"
#include <cstdio>

namespace mulan::engine {

// ============================================================
// Helper: RHI enum → D3D11 enum
// ============================================================

static D3D11_FILTER toDX11Filter(SamplerFilter min, SamplerFilter mag,
                                  SamplerFilter mip, bool anisotropy, bool comparison) {
    if (anisotropy) {
        return comparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
    }
    if (min == SamplerFilter::Linear && mag == SamplerFilter::Linear && mip == SamplerFilter::Linear)
        return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (min == SamplerFilter::Linear && mag == SamplerFilter::Linear && mip == SamplerFilter::Nearest)
        return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Nearest && mip == SamplerFilter::Nearest)
        return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Nearest && mip == SamplerFilter::Linear)
        return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    if (min == SamplerFilter::Linear && mag == SamplerFilter::Nearest && mip == SamplerFilter::Nearest)
        return comparison ? D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT : D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Linear && mip == SamplerFilter::Nearest)
        return comparison ? D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;

    return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
}

static D3D11_TEXTURE_ADDRESS_MODE toDX11AddressMode(SamplerAddressMode m) {
    switch (m) {
    case SamplerAddressMode::Repeat:            return D3D11_TEXTURE_ADDRESS_WRAP;
    case SamplerAddressMode::MirroredRepeat:    return D3D11_TEXTURE_ADDRESS_MIRROR;
    case SamplerAddressMode::ClampToEdge:       return D3D11_TEXTURE_ADDRESS_CLAMP;
    case SamplerAddressMode::ClampToBorder:     return D3D11_TEXTURE_ADDRESS_BORDER;
    case SamplerAddressMode::MirrorClampToEdge: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
    }
    return D3D11_TEXTURE_ADDRESS_WRAP;
}

static D3D11_COMPARISON_FUNC toDX11ComparisonFunc(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never:        return D3D11_COMPARISON_NEVER;
    case CompareFunc::Less:         return D3D11_COMPARISON_LESS;
    case CompareFunc::Equal:        return D3D11_COMPARISON_EQUAL;
    case CompareFunc::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
    case CompareFunc::Greater:      return D3D11_COMPARISON_GREATER;
    case CompareFunc::NotEqual:     return D3D11_COMPARISON_NOT_EQUAL;
    case CompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareFunc::Always:       return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_NEVER;
}

// ============================================================
// DX11Sampler
// ============================================================

DX11Sampler::DX11Sampler(const SamplerDesc& desc, ID3D11Device* device)
    : m_desc(desc)
{
    D3D11_SAMPLER_DESC d3dDesc = {};
    d3dDesc.Filter = toDX11Filter(desc.minFilter, desc.magFilter, desc.mipFilter,
                                   desc.anisotropyEnable, desc.compareEnable);
    d3dDesc.AddressU = toDX11AddressMode(desc.addressU);
    d3dDesc.AddressV = toDX11AddressMode(desc.addressV);
    d3dDesc.AddressW = toDX11AddressMode(desc.addressW);
    d3dDesc.MipLODBias   = desc.mipLodBias;
    d3dDesc.MaxAnisotropy = desc.anisotropyEnable
                                ? static_cast<UINT>(desc.maxAniso)
                                : 16u;
    d3dDesc.ComparisonFunc = desc.compareEnable
                                ? toDX11ComparisonFunc(desc.compareFunc)
                                : D3D11_COMPARISON_NEVER;
    d3dDesc.BorderColor[0] = desc.borderColor[0];
    d3dDesc.BorderColor[1] = desc.borderColor[1];
    d3dDesc.BorderColor[2] = desc.borderColor[2];
    d3dDesc.BorderColor[3] = desc.borderColor[3];
    d3dDesc.MinLOD = desc.minLod;
    d3dDesc.MaxLOD = desc.maxLod;

    HRESULT hr = device->CreateSamplerState(&d3dDesc, &m_handle);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[DX11Sampler] CreateSamplerState failed, hr=0x%08X\n", hr);
    }
}

DX11Sampler::~DX11Sampler() = default;

} // namespace mulan::Engine
