/**
 * @file dx12_fence.h
 * @brief D3D12 Fence 实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../fence.h"
#include "dx12_common.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class DX12Fence final : public Fence {
public:
    /// 创建 DX12Fence。失败返回 FenceCreateFailed。
    static core::Result<std::unique_ptr<DX12Fence>>
        create(ID3D12Device* device, uint64_t initialValue);

    DX12Fence(ID3D12Device* device, uint64_t initialValue);

    ~DX12Fence();

    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
    uint64_t completedValue() const override;

    ID3D12Fence* fence() const { return fence_.Get(); }
    HANDLE event() const { return event_; }

private:

    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr;
};

} // namespace mulan::engine
