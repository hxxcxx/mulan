/**
 * @file DX11Sampler.h
 * @brief D3D11 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../Sampler.h"
#include "DX11Common.h"

namespace MulanGeo::engine {

class DX11Sampler : public Sampler {
public:
    DX11Sampler(const SamplerDesc& desc, ID3D11Device* device);
    ~DX11Sampler();

    const SamplerDesc& desc() const override { return m_desc; }

    ID3D11SamplerState* handle() const { return m_handle.Get(); }

private:
    SamplerDesc                  m_desc;
    ComPtr<ID3D11SamplerState>   m_handle;
};

} // namespace MulanGeo::Engine
