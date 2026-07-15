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

    DX12BindGroup(const BindGroupLayout& layout, const BindGroupEntry* entries, uint8_t count,
                  BindGroupValidationLimits limits);

    const BindGroupLayout& layout() const override { return layout_; }
    const BindGroupEntry* entries() const override { return entries_.data(); }
    uint8_t entryCount() const override { return count_; }

    bool updateUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) override;
    bool updateTexture(uint32_t binding, Texture* tex) override;
    bool updateSampler(uint32_t binding, Sampler* s) override;

    // dirty()/markClean()/dirtyMask()/clearDirty() 由基类提供

    /// binding 号 → D3D12 root parameter index。
    /// 所有 descriptor 类型（包括 sampler）都占用一个 root parameter；
    /// 映射按 canonical layout 的 binding 顺序计算，与 createRootSignature 一致。
    uint32_t rootIndexForBinding(uint32_t binding) const;

    // 纹理 descriptor 的句柄是当前帧 heap 中的不可变快照。
    // 同一 BindGroup 在一条 command list 内切换纹理时，旧 draw 仍可能尚未执行，
    // 因此不能原地覆盖旧 descriptor；DX12CommandList 会为变化的 entry 分配新 slot。
    D3D12_GPU_DESCRIPTOR_HANDLE cachedTextureHandle(uint8_t entryIndex) const {
        return entryIndex < kMaxEntries ? cached_texture_handles_[entryIndex] : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }
    void setCachedTextureHandle(uint8_t entryIndex, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
        if (entryIndex < kMaxEntries)
            cached_texture_handles_[entryIndex] = handle;
    }
    void clearCachedTextureHandles() { cached_texture_handles_.fill({}); }

private:
    BindGroupLayout layout_;
    std::array<BindGroupEntry, kMaxEntries> entries_{};
    uint8_t count_ = 0;
    // binding 号 → root parameter index 的查找表（线性扫描，条目数 ≤16）。
    struct BindingRootMap {
        uint32_t binding = 0;
        uint32_t rootIndex = 0;
    };
    std::array<BindingRootMap, kMaxEntries> root_map_{};
    uint8_t root_map_count_ = 0;
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kMaxEntries> cached_texture_handles_{};
};

}  // namespace mulan::engine
