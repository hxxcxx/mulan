/**
 * @file gl_bind_group.cpp
 * @brief OpenGL BindGroup 实现
 * @author hxxcxx
 * @date 2026-07-12
 */

#include "detail/gl_bind_group.h"

namespace mulan::engine {

GLBindGroup::GLBindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc)
    : layout_(layout), entries_(desc.entries, desc.entries + desc.count) {
}

int GLBindGroup::findEntry(uint32_t binding) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].binding == binding)
            return static_cast<int>(i);
    }
    return -1;
}

bool GLBindGroup::updateUBO(uint32_t binding, Buffer* buffer, uint32_t offset, uint32_t size) {
    const int index = findEntry(binding);
    if (index < 0)
        return false;
    auto& entry = entries_[static_cast<size_t>(index)];
    if (entry.type != DescriptorType::UniformBuffer)
        return false;
    entry.buffer = buffer;
    entry.texture = nullptr;
    entry.sampler = nullptr;
    entry.offset = offset;
    entry.size = size;
    markAllDirty();
    return true;
}

bool GLBindGroup::updateTexture(uint32_t binding, Texture* texture) {
    const int index = findEntry(binding);
    if (index < 0)
        return false;
    auto& entry = entries_[static_cast<size_t>(index)];
    if (entry.type != DescriptorType::TextureSRV)
        return false;
    entry.buffer = nullptr;
    entry.texture = texture;
    entry.sampler = nullptr;
    entry.offset = 0;
    entry.size = 0;
    markAllDirty();
    return true;
}

bool GLBindGroup::updateSampler(uint32_t binding, Sampler* sampler) {
    const int index = findEntry(binding);
    if (index < 0)
        return false;
    auto& entry = entries_[static_cast<size_t>(index)];
    if (entry.type != DescriptorType::Sampler)
        return false;
    entry.buffer = nullptr;
    entry.texture = nullptr;
    entry.sampler = sampler;
    entry.offset = 0;
    entry.size = 0;
    markAllDirty();
    return true;
}

}  // namespace mulan::engine
