/**
 * @file dx12_bind_group.h
 * @brief D3D12 后端 BindGroup —— 内联条目 + 缓存 descriptor heap 句柄
 * @author hxxcxx
 * @date 2026-07-04
 */
#pragma once

#include "../rhi/bind_group.h"
#include "dx12_common.h"

#include <array>
#include <cstdint>

namespace mulan::engine {

class DX12BindGroup : public BindGroup {
public:
    static constexpr uint8_t kMaxEntries = 16;
    static constexpr uint32_t kInvalidRootIndex = 0xFFFFFFFFu;

    DX12BindGroup(const BindGroupLayout& layout, const BindGroupEntry* entries, uint8_t count);

    const BindGroupLayout& layout() const override { return *layout_; }
    const BindGroupEntry* entries() const override { return entries_.data(); }
    uint8_t entryCount() const override { return count_; }

    bool updateUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) override;
    bool updateTexture(uint32_t binding, Texture* tex) override;
    bool updateSampler(uint32_t binding, Sampler* s) override;

    // dirty()/markClean()/dirtyMask()/clearDirty() 由基类提供

    /// binding 号 → D3D12 root parameter index。
    /// root signature 中 static sampler 不占 root parameter slot，因此 binding 号
    /// 与 root parameter index 在存在 Sampler 绑定时会错位。此映射在构造时按
    /// layout 的 binding 排序计算（跳过 Sampler，其余依次 0,1,2,...），
    /// 与 createRootSignature 的 root parameter 分配规则一致。
    uint32_t rootIndexForBinding(uint32_t binding) const;

    // 缓存 GPU descriptor handle（由 DX12CommandList::bindGroup 管理）
    D3D12_GPU_DESCRIPTOR_HANDLE cachedGpuHandle() const { return cached_gpu_handle_; }
    void setCachedGpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE h) { cached_gpu_handle_ = h; }

private:
    const BindGroupLayout* layout_;
    std::array<BindGroupEntry, kMaxEntries> entries_{};
    uint8_t count_ = 0;
    // binding 号 → root parameter index 的查找表（线性扫描，条目数 ≤16）。
    // Sampler 类型的 binding 记为 kInvalidRootIndex（不占 root slot）。
    struct BindingRootMap {
        uint32_t binding = 0;
        uint32_t rootIndex = 0;
    };
    std::array<BindingRootMap, kMaxEntries> root_map_{};
    uint8_t root_map_count_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE cached_gpu_handle_ = {};
};

}  // namespace mulan::engine
