#include "dx12_frame_context.h"

namespace mulan::engine {

DX12FrameContext::DX12FrameContext(ID3D12Device* device) {
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_allocator_));
    DX12_CHECK(hr);

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_allocator_.Get(), nullptr,
                                   IID_PPV_ARGS(&cmd_list_));
    DX12_CHECK(hr);
    cmd_list_->Close();  // 创建时 open，先关闭

    fence_ = std::make_unique<DX12Fence>(device, 0);
}

DX12FrameContext::~DX12FrameContext() = default;

void DX12FrameContext::waitForFence() {
    fence_->wait(fence_value_);
}

void DX12FrameContext::resetCommandAllocator() {
    cmd_allocator_->Reset();
    // 重新 open command list
    cmd_list_->Reset(cmd_allocator_.Get(), nullptr);
}

}  // namespace mulan::engine
