/**
 * @file gl_bind_group.h
 * @brief OpenGL BindGroup 实现，维护统一的 UBO、纹理和采样器绑定描述
 * @author hxxcxx
 * @date 2026-07-12
 */

#pragma once

#include "../../rhi/bind_group.h"

#include <vector>

namespace mulan::engine {

class GLBindGroup final : public BindGroup {
public:
    GLBindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc);
    ~GLBindGroup() override = default;

    const BindGroupLayout& layout() const override { return layout_; }
    const BindGroupEntry* entries() const override { return entries_.data(); }
    uint8_t entryCount() const override { return static_cast<uint8_t>(entries_.size()); }

    bool updateUBO(uint32_t binding, Buffer* buffer, uint32_t offset, uint32_t size) override;
    bool updateTexture(uint32_t binding, Texture* texture) override;
    bool updateSampler(uint32_t binding, Sampler* sampler) override;

private:
    int findEntry(uint32_t binding) const;

    BindGroupLayout layout_;
    std::vector<BindGroupEntry> entries_;
};

}  // namespace mulan::engine
