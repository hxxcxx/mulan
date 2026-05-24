/**
 * @file DX12FrameContext.cpp
 * @brief D3D12 帧上下文实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12FrameContext.h"

namespace MulanGeo::engine {

DX12FrameContext::DX12FrameContext(ID3D12Device* device) {
    HRESULT hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_cmdAllocator));
    DX12_CHECK(hr);

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_cmdAllocator.Get(), nullptr,
                                   IID_PPV_ARGS(&m_cmdList));
    DX12_CHECK(hr);
    m_cmdList->Close();  // 创建时 open，先关闭

    m_fence = std::make_unique<DX12Fence>(device, 0);
}

DX12FrameContext::~DX12FrameContext() = default;

void DX12FrameContext::waitForFence() {
    m_fence->wait(m_fenceValue);
}

void DX12FrameContext::resetCommandAllocator() {
    m_cmdAllocator->Reset();
    // 重新 open command list
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);
}

} // namespace MulanGeo::Engine
