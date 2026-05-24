/**
 * @file DX12Sampler.h
 * @brief D3D12 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../Sampler.h"
#include "DX12Common.h"
#include "DX12DescriptorAllocator.h"

namespace MulanGeo::engine {

class DX12Sampler : public Sampler {
public:
    DX12Sampler(const SamplerDesc& desc, ID3D12Device* device,
                DX12DescriptorAllocator* samplerHeap);
    ~DX12Sampler();

    const SamplerDesc& desc() const override { return m_desc; }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle() const { return m_descriptor.cpu; }
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle() const { return m_descriptor.gpu; }

private:
    SamplerDesc     m_desc;
    DX12Descriptor  m_descriptor;
};

} // namespace MulanGeo::Engine
