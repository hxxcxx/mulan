/**
 * @file DX12Fence.cpp
 * @brief D3D12 Fence 实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12Fence.h"

namespace mulan::engine {

DX12Fence::DX12Fence(ID3D12Device* device, uint64_t initialValue) {
    HRESULT hr = device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&m_fence));
    DX12_CHECK(hr);
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

void DX12Fence::signal(uint64_t value) {
    m_fence->Signal(value);
}

void DX12Fence::wait(uint64_t value) {
    if (m_fence->GetCompletedValue() < value) {
        m_fence->SetEventOnCompletion(value, m_event);
        WaitForSingleObject(m_event, INFINITE);
    }
}

uint64_t DX12Fence::completedValue() const {
    return m_fence->GetCompletedValue();
}

DX12Fence::~DX12Fence() {
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
}

} // namespace mulan::Engine
