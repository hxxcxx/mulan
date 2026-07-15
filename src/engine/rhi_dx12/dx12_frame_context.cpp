#include "detail/dx12_frame_context.h"
#include "../rhi/engine_error_code.h"

namespace mulan::engine {

DX12FrameContext::DX12FrameContext(ID3D12Device* device) : transient_uniform_arena_(device) {
    // 每个 FrameContext 独占 shader-visible heap，避免仍在执行的帧被后续帧覆盖。
    descriptor_arena_ = std::make_unique<DX12DescriptorAllocator>(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 8192);
    if (!descriptor_arena_->isValid())
        return;

    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_allocator_));
    if (!checkDX12(hr, "ID3D12Device::CreateCommandAllocator"))
        return;

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_allocator_.Get(), nullptr,
                                   IID_PPV_ARGS(&cmd_list_));
    if (!checkDX12(hr, "ID3D12Device::CreateCommandList"))
        return;
    cmd_list_->Close();  // 创建时 open，先关闭

    fence_ = std::make_unique<DX12Fence>(device, 0);
}

DX12FrameContext::~DX12FrameContext() = default;

core::Result<void> DX12FrameContext::waitForFence() {
    return fence_->wait(fence_value_);
}

core::Result<void> DX12FrameContext::resetCommandAllocator() {
    if (!checkDX12(cmd_allocator_->Reset(), "ID3D12CommandAllocator::Reset"))
        return std::unexpected(
                makeError(EngineErrorCode::CommandRecordingFailed, "DX12 frame command allocator reset failed"));
    // 重新 open command list
    if (!checkDX12(cmd_list_->Reset(cmd_allocator_.Get(), nullptr), "ID3D12GraphicsCommandList::Reset"))
        return std::unexpected(
                makeError(EngineErrorCode::CommandRecordingFailed, "DX12 frame command list reset failed"));
    return {};
}

}  // namespace mulan::engine
