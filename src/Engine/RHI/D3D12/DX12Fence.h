/**
 * @file DX12Fence.h
 * @brief D3D12 Fence 实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../Fence.h"
#include "DX12Common.h"

namespace MulanGeo::engine {

class DX12Fence final : public Fence {
public:
    DX12Fence(ID3D12Device* device, uint64_t initialValue);
    ~DX12Fence();

    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
    uint64_t completedValue() const override;

    ID3D12Fence* fence() const { return m_fence.Get(); }
    HANDLE event() const { return m_event; }

private:
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_event = nullptr;
};

} // namespace MulanGeo::Engine
