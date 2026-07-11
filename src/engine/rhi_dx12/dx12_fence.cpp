#include "dx12_fence.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <string>

namespace mulan::engine {

core::Result<std::unique_ptr<DX12Fence>> DX12Fence::create(ID3D12Device* device, uint64_t initialValue) {
    try {
        return std::unique_ptr<DX12Fence>(new DX12Fence(device, initialValue));
    } catch (const std::exception& e) {
        return std::unexpected(
                makeError(EngineErrorCode::FenceCreateFailed, std::string("DX12Fence create failed: ") + e.what()));
    }
}

DX12Fence::DX12Fence(ID3D12Device* device, uint64_t initialValue) {
    HRESULT hr = device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
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

}  // namespace mulan::engine
