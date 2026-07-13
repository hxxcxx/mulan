/**
 * @file dx12_descriptor_allocator.h
 * @brief D3D12 描述符堆分配器，管理 CBV/SRV/UAV/RTV/DSV/Sampler
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "dx12_common.h"

#include <cstdint>
#include <vector>

namespace mulan::engine {

/// 描述符句柄（CPU + GPU 可见性分离）
struct DX12Descriptor {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
    uint32_t index = 0;
};

class DX12DescriptorAllocator {
public:
    DX12DescriptorAllocator(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags,
                            uint32_t capacity = 256);
    ~DX12DescriptorAllocator() = default;

    /// 重置分配器（每帧开始时调用）
    void reset();

    /// 分配一个描述符
    DX12Descriptor allocate();

    /// 当前已分配数量
    uint32_t allocatedCount() const { return allocated_; }

    /// 描述符大小
    uint32_t descriptorSize() const { return descriptor_size_; }
    /// 描述符容量
    uint32_t capacity() const { return capacity_; }

    /// 获取底层堆（用于绑定到命令列表）
    ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
    bool isValid() const { return heap_ != nullptr; }

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_ = {};
    uint32_t capacity_ = 0;
    uint32_t allocated_ = 0;
    uint32_t descriptor_size_ = 0;
};

}  // namespace mulan::engine
