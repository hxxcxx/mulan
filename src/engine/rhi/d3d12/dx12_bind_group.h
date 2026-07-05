/**
 * @file dx12_bind_group.h
 * @brief D3D12 后端 BindGroup —— 内联条目 + 缓存 descriptor heap 句柄
 * @author hxxcxx
 * @date 2026-07-04
 */
#pragma once

#include "../bind_group.h"
#include "dx12_common.h"

#include <array>
#include <cstdint>

namespace mulan::engine {

class DX12BindGroup : public BindGroup {
public:
    static constexpr uint8_t kMaxEntries = 16;

    DX12BindGroup(const BindGroupLayout& layout, const BindGroupEntry* entries, uint8_t count);

    const BindGroupLayout& layout() const override { return *layout_; }
    const BindGroupEntry* entries() const override { return entries_.data(); }
    uint8_t entryCount() const override { return count_; }

    bool updateUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) override;
    bool updateTexture(uint32_t binding, Texture* tex) override;
    bool updateSampler(uint32_t binding, Sampler* s) override;

    // dirty()/markClean()/dirtyMask()/clearDirty() 由基类提供

    // 缓存 GPU descriptor handle（由 DX12CommandList::bindGroup 管理）
    D3D12_GPU_DESCRIPTOR_HANDLE cachedGpuHandle() const { return cached_gpu_handle_; }
    void setCachedGpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE h) { cached_gpu_handle_ = h; }

private:
    const BindGroupLayout* layout_;
    std::array<BindGroupEntry, kMaxEntries> entries_{};
    uint8_t count_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE cached_gpu_handle_ = {};
};

}  // namespace mulan::engine
