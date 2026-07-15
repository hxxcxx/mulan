/**
 * @file dx12_frame_context.h
 * @brief D3D12 帧上下文，每帧独立的命令分配器 + 同步
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "dx12_common.h"
#include "dx12_fence.h"
#include "dx12_descriptor_allocator.h"
#include "dx12_transient_uniform_arena.h"

#include <memory>

namespace mulan::engine {

class DX12FrameContext {
public:
    DX12FrameContext(ID3D12Device* device);
    ~DX12FrameContext();

    Result<void> waitForFence();
    Result<void> resetCommandAllocator();

    ID3D12CommandAllocator* commandAllocator() const { return cmd_allocator_.Get(); }
    ID3D12GraphicsCommandList* commandList() const { return cmd_list_.Get(); }
    DX12Fence* fence() const { return fence_.get(); }
    DX12TransientUniformArena* transientUniformArena() { return &transient_uniform_arena_; }
    DX12DescriptorAllocator* descriptorArena() { return descriptor_arena_.get(); }
    uint64_t fenceValue() const { return fence_value_; }
    void setFenceValue(uint64_t v) { fence_value_ = v; }
    bool isValid() const {
        return cmd_allocator_ && cmd_list_ && fence_ && fence_->isValid() && descriptor_arena_ &&
               descriptor_arena_->isValid();
    }

private:
    ComPtr<ID3D12CommandAllocator> cmd_allocator_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    std::unique_ptr<DX12Fence> fence_;
    std::unique_ptr<DX12DescriptorAllocator> descriptor_arena_;
    DX12TransientUniformArena transient_uniform_arena_;
    uint64_t fence_value_ = 0;
};

}  // namespace mulan::engine
