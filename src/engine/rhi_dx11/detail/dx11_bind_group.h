/**
 * @file dx11_bind_group.h
 * @brief D3D11 BindGroup 实现，保存布局与资源绑定条目
 * @author hxxcxx
 * @date 2026-07-13
 */

#pragma once

#include "../../rhi/bind_group.h"

#include <array>
#include <cstdint>

namespace mulan::engine {

class DX11BindGroup final : public BindGroup {
public:
    static constexpr uint8_t kMaxEntries = BindGroupDesc::kMaxEntries;

    DX11BindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc, BindGroupValidationLimits limits = {});
    ~DX11BindGroup() override = default;

    const BindGroupLayout& layout() const override { return layout_; }
    const BindGroupEntry* entries() const override { return entries_.data(); }
    uint8_t entryCount() const override { return count_; }
    bool isValid() const { return count_ <= kMaxEntries; }

    bool updateUBO(uint32_t binding, Buffer* buffer, uint32_t offset, uint32_t size) override;
    bool updateTexture(uint32_t binding, Texture* texture) override;
    bool updateSampler(uint32_t binding, Sampler* sampler) override;

private:
    int findEntry(uint32_t binding) const;

    BindGroupLayout layout_;
    std::array<BindGroupEntry, kMaxEntries> entries_{};
    uint8_t count_ = 0;
};

}  // namespace mulan::engine
