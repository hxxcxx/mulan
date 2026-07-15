#include "detail/dx11_sampler.h"

#include <algorithm>

namespace mulan::engine {

// ============================================================
// Helper: RHI enum → D3D11 enum
// ============================================================

static D3D11_FILTER toDX11Filter(SamplerFilter min, SamplerFilter mag, SamplerFilter mip, bool anisotropy,
                                 bool comparison) {
    if (anisotropy || min == SamplerFilter::Anisotropic || mag == SamplerFilter::Anisotropic ||
        mip == SamplerFilter::Anisotropic) {
        return comparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
    }

    const auto select = [comparison](D3D11_FILTER regular, D3D11_FILTER comparisonFilter) {
        return comparison ? comparisonFilter : regular;
    };
    const bool minLinear = min == SamplerFilter::Linear;
    const bool magLinear = mag == SamplerFilter::Linear;
    const bool mipLinear = mip == SamplerFilter::Linear;
    if (minLinear && magLinear && mipLinear)
        return select(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);
    if (minLinear && magLinear)
        return select(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT);
    if (minLinear && mipLinear)
        return select(D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
                      D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR);
    if (minLinear)
        return select(D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT, D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT);
    if (magLinear && mipLinear)
        return select(D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR, D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR);
    if (magLinear)
        return select(D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
                      D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT);
    if (mipLinear)
        return select(D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR, D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR);
    return select(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT);
}

static D3D11_TEXTURE_ADDRESS_MODE toDX11AddressMode(SamplerAddressMode m) {
    switch (m) {
    case SamplerAddressMode::Repeat: return D3D11_TEXTURE_ADDRESS_WRAP;
    case SamplerAddressMode::MirroredRepeat: return D3D11_TEXTURE_ADDRESS_MIRROR;
    case SamplerAddressMode::ClampToEdge: return D3D11_TEXTURE_ADDRESS_CLAMP;
    case SamplerAddressMode::ClampToBorder: return D3D11_TEXTURE_ADDRESS_BORDER;
    case SamplerAddressMode::MirrorClampToEdge: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
    }
    return D3D11_TEXTURE_ADDRESS_WRAP;
}

static D3D11_COMPARISON_FUNC toDX11ComparisonFunc(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never: return D3D11_COMPARISON_NEVER;
    case CompareFunc::Less: return D3D11_COMPARISON_LESS;
    case CompareFunc::Equal: return D3D11_COMPARISON_EQUAL;
    case CompareFunc::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;
    case CompareFunc::Greater: return D3D11_COMPARISON_GREATER;
    case CompareFunc::NotEqual: return D3D11_COMPARISON_NOT_EQUAL;
    case CompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareFunc::Always: return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_NEVER;
}

// ============================================================
// DX11Sampler
// ============================================================

DX11Sampler::DX11Sampler(const SamplerDesc& desc, ID3D11Device* device) : m_desc(desc) {
    D3D11_SAMPLER_DESC d3dDesc = {};
    const bool anisotropic = desc.anisotropyEnable || desc.minFilter == SamplerFilter::Anisotropic ||
                             desc.magFilter == SamplerFilter::Anisotropic ||
                             desc.mipFilter == SamplerFilter::Anisotropic;
    d3dDesc.Filter = toDX11Filter(desc.minFilter, desc.magFilter, desc.mipFilter, anisotropic, desc.compareEnable);
    d3dDesc.AddressU = toDX11AddressMode(desc.addressU);
    d3dDesc.AddressV = toDX11AddressMode(desc.addressV);
    d3dDesc.AddressW = toDX11AddressMode(desc.addressW);
    d3dDesc.MipLODBias = desc.mipLodBias;
    d3dDesc.MaxAnisotropy =
            anisotropic
                    ? static_cast<UINT>(std::clamp(desc.maxAniso, 1.0f, static_cast<float>(D3D11_REQ_MAXANISOTROPY)))
                    : 1u;
    d3dDesc.ComparisonFunc = desc.compareEnable ? toDX11ComparisonFunc(desc.compareFunc) : D3D11_COMPARISON_NEVER;
    d3dDesc.BorderColor[0] = desc.borderColor[0];
    d3dDesc.BorderColor[1] = desc.borderColor[1];
    d3dDesc.BorderColor[2] = desc.borderColor[2];
    d3dDesc.BorderColor[3] = desc.borderColor[3];
    d3dDesc.MinLOD = desc.minLod;
    d3dDesc.MaxLOD = desc.maxLod;

    if (!device) {
        LOG_ERROR("[DX11] Sampler initialization rejected: invalid device");
        return;
    }
    if (!checkDX11(device->CreateSamplerState(&d3dDesc, &m_handle), "ID3D11Device::CreateSamplerState"))
        return;
}

DX11Sampler::~DX11Sampler() {
    waitForLastUseBeforeDestruction();
}

}  // namespace mulan::engine
