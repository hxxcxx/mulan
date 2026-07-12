/**
 * @file dx12_sampler.h
 * @brief D3D12 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../rhi/sampler.h"
#include "dx12_common.h"
#include "dx12_descriptor_allocator.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class DX12Sampler : public Sampler {
public:
    /// 创建 DX12Sampler。samplerHeap 为空时不分配 descriptor。
    static core::Result<std::unique_ptr<DX12Sampler>> create(const SamplerDesc& desc, ID3D12Device* device,
                                                             DX12DescriptorAllocator* samplerHeap);
    ~DX12Sampler();

    const SamplerDesc& desc() const override { return desc_; }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle() const { return descriptor_.cpu; }
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle() const { return descriptor_.gpu; }

private:
    DX12Sampler(const SamplerDesc& desc, ID3D12Device* device, DX12DescriptorAllocator* samplerHeap);

    SamplerDesc desc_;
    DX12Descriptor descriptor_;
};

}  // namespace mulan::engine
