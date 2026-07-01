#include "dx12_fence.h"

namespace mulan::engine {

DX12Fence::DX12Fence(ID3D12Device* device, uint64_t initialValue) {
    HRESULT hr = device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&fence_));
    DX12_CHECK(hr);
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

void DX12Fence::signal(uint64_t value) {
    fence_->Signal(value);
}

void DX12Fence::wait(uint64_t value) {
    if (fence_->GetCompletedValue() < value) {
        fence_->SetEventOnCompletion(value, event_);
        WaitForSingleObject(event_, INFINITE);
    }
}

uint64_t DX12Fence::completedValue() const {
    return fence_->GetCompletedValue();
}

DX12Fence::~DX12Fence() {
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
}

} // namespace mulan::engine
