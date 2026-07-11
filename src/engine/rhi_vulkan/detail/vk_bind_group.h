/**
 * @file vk_bind_group.h
 * @brief VK 后端 BindGroup —— 内联条目 + 缓存 vk::DescriptorSet
 * @author hxxcxx
 * @date 2026-07-04
 */
#pragma once

#include "../rhi/bind_group.h"
#include "vk_common.h"

#include <array>

namespace mulan::engine {

class VKBindGroup : public BindGroup {
public:
    static constexpr uint8_t kMaxEntries = 16;

    VKBindGroup(const BindGroupLayout& layout, const BindGroupEntry* entries, uint8_t count);

    const BindGroupLayout& layout() const override { return *layout_; }
    const BindGroupEntry* entries() const override { return entries_.data(); }
    uint8_t entryCount() const override { return count_; }

    bool updateUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) override;
    bool updateTexture(uint32_t binding, Texture* tex) override;
    bool updateSampler(uint32_t binding, Sampler* s) override;

    // dirty()/markClean()/dirtyMask()/clearDirty() 由基类提供

    // 缓存（由 VKCommandList::bindGroup 管理）
    vk::DescriptorSet cachedSet() const { return cached_set_; }
    void setCachedSet(vk::DescriptorSet set) { cached_set_ = set; }

private:
    const BindGroupLayout* layout_;
    std::array<BindGroupEntry, kMaxEntries> entries_{};
    uint8_t count_ = 0;
    vk::DescriptorSet cached_set_ = nullptr;
};

}  // namespace mulan::engine
