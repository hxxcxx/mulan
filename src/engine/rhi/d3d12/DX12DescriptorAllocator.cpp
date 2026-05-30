/**
 * @file DX12DescriptorAllocator.cpp
 * @brief D3D12 描述符堆分配器实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12DescriptorAllocator.h"

namespace mulan::engine {

DX12DescriptorAllocator::DX12DescriptorAllocator(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    D3D12_DESCRIPTOR_HEAP_FLAGS flags,
    uint32_t capacity)
    : m_capacity(capacity)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = type;
    desc.NumDescriptors = capacity;
    desc.Flags          = flags;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    DX12_CHECK(hr);

    m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    if (flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    }

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
}

void DX12DescriptorAllocator::reset() {
    m_allocated = 0;
}

DX12Descriptor DX12DescriptorAllocator::allocate() {
    assert(m_allocated < m_capacity && "Descriptor heap exhausted");

    DX12Descriptor desc;
    desc.index = m_allocated;
    desc.cpu.ptr = m_cpuStart.ptr + static_cast<uint64_t>(m_allocated) * m_descriptorSize;
    desc.gpu.ptr = m_gpuStart.ptr + static_cast<uint64_t>(m_allocated) * m_descriptorSize;

    m_allocated++;
    return desc;
}

} // namespace mulan::engine
