/**
 * @file dx11_fence.h
 * @brief D3D11 Fence 实现（使用 ID3D11Query 模拟）
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/fence.h"
#include "dx11_common.h"

#include <deque>

namespace mulan::engine {

class DX11Fence final : public Fence {
public:
    DX11Fence(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t initialValue);
    ~DX11Fence() = default;

    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
    uint64_t completedValue() const override;
    uint64_t signaledValue() const { return m_signaled; }
    bool isValid() const { return m_device && m_ctx; }

private:
    struct PendingSignal {
        uint64_t value = 0;
        ComPtr<ID3D11Query> query;
    };

    void poll(bool waitForTarget, uint64_t targetValue);

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_ctx;
    std::deque<PendingSignal> m_pendingSignals;
    uint64_t m_signaled = 0;
    uint64_t m_completed = 0;
};

}  // namespace mulan::engine
