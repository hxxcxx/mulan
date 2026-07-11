#include "detail/dx12_descriptor_allocator.h"

namespace mulan::engine {

DX12DescriptorAllocator::DX12DescriptorAllocator(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                 D3D12_DESCRIPTOR_HEAP_FLAGS flags, uint32_t capacity)
    : capacity_(capacity) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = capacity;
    desc.Flags = flags;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));
    DX12_CHECK(hr);

    cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
    if (flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
    }

    descriptor_size_ = device->GetDescriptorHandleIncrementSize(type);
}

void DX12DescriptorAllocator::reset() {
    allocated_ = 0;
}

DX12Descriptor DX12DescriptorAllocator::allocate() {
    assert(allocated_ < capacity_ && "Descriptor heap exhausted");

    DX12Descriptor desc;
    desc.index = allocated_;
    desc.cpu.ptr = cpu_start_.ptr + static_cast<uint64_t>(allocated_) * descriptor_size_;
    desc.gpu.ptr = gpu_start_.ptr + static_cast<uint64_t>(allocated_) * descriptor_size_;

    allocated_++;
    return desc;
}

}  // namespace mulan::engine
