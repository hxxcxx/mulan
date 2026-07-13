/**
 * @file dx11_sampler.h
 * @brief D3D11 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../../rhi/sampler.h"
#include "dx11_common.h"

namespace mulan::engine {

class DX11Sampler : public Sampler {
public:
    DX11Sampler(const SamplerDesc& desc, ID3D11Device* device);
    ~DX11Sampler();

    const SamplerDesc& desc() const override { return m_desc; }

    ID3D11SamplerState* handle() const { return m_handle.Get(); }
    bool isValid() const { return m_handle != nullptr; }

private:
    SamplerDesc m_desc;
    ComPtr<ID3D11SamplerState> m_handle;
};

}  // namespace mulan::engine
