#include "detail/dx12_fence.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <string>

namespace mulan::engine {

Result<std::unique_ptr<DX12Fence>> DX12Fence::create(ID3D12Device* device, uint64_t initialValue) {
    if (!device)
        return std::unexpected(makeError(EngineErrorCode::FenceCreateFailed, "DX12Fence requires a valid device"));
    auto object = std::unique_ptr<DX12Fence>(new DX12Fence(device, initialValue));
    if (!object->fence_ || !object->event_)
        return std::unexpected(makeError(EngineErrorCode::FenceCreateFailed, "DX12Fence initialization failed"));
    return object;
}

DX12Fence::DX12Fence(ID3D12Device* device, uint64_t initialValue) {
    HRESULT hr = device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (!checkDX12(hr, "ID3D12Device::CreateFence"))
        return;
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

ResultVoid DX12Fence::signal(uint64_t value) {
    if (!checkDX12(fence_->Signal(value), "ID3D12Fence::Signal"))
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX12 fence signal failed"));
    return {};
}

ResultVoid DX12Fence::wait(uint64_t value) {
    if (fence_->GetCompletedValue() < value) {
        if (!checkDX12(fence_->SetEventOnCompletion(value, event_), "ID3D12Fence::SetEventOnCompletion"))
            return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, "DX12 fence wait setup failed"));
        if (WaitForSingleObject(event_, INFINITE) != WAIT_OBJECT_0)
            return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, "DX12 fence wait failed"));
    }
    return {};
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
