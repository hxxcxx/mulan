/**
 * @file DX12FrameContext.h
 * @brief D3D12 帧上下文，每帧独立的命令分配器 + 同步
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "DX12Common.h"
#include "DX12Fence.h"

#include <memory>

namespace mulan::engine {

class DX12FrameContext {
public:
    DX12FrameContext(ID3D12Device* device);
    ~DX12FrameContext();

    void waitForFence();
    void resetCommandAllocator();

    ID3D12CommandAllocator* commandAllocator() const { return m_cmdAllocator.Get(); }
    ID3D12GraphicsCommandList* commandList() const { return m_cmdList.Get(); }
    DX12Fence* fence() const { return m_fence.get(); }
    uint64_t fenceValue() const { return m_fenceValue; }
    void setFenceValue(uint64_t v) { m_fenceValue = v; }

private:
    ComPtr<ID3D12CommandAllocator>     m_cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList>  m_cmdList;
    std::unique_ptr<DX12Fence>         m_fence;
    uint64_t                           m_fenceValue = 0;
};

} // namespace mulan::Engine
