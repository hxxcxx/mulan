#include "detail/dx11_bind_group.h"

#include <algorithm>

namespace mulan::engine {

DX11BindGroup::DX11BindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc) : layout_(layout) {
    count_ = (std::min) (desc.count, kMaxEntries);
    for (uint8_t i = 0; i < count_; ++i)
        entries_[i] = desc.entries[i];
}

int DX11BindGroup::findEntry(uint32_t binding) const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].binding == binding)
            return static_cast<int>(i);
    }
    return -1;
}

bool DX11BindGroup::updateUBO(uint32_t binding, Buffer* buffer, uint32_t offset, uint32_t size) {
    const int index = findEntry(binding);
    if (index < 0)
        return false;

    auto& entry = entries_[static_cast<uint8_t>(index)];
    entry.buffer = buffer;
    entry.texture = nullptr;
    entry.sampler = nullptr;
    entry.offset = offset;
    entry.size = size;
    dirty_mask_ |= static_cast<uint16_t>(uint16_t{ 1 } << static_cast<uint8_t>(index));
    return true;
}

bool DX11BindGroup::updateTexture(uint32_t binding, Texture* texture) {
    const int index = findEntry(binding);
    if (index < 0)
        return false;

    auto& entry = entries_[static_cast<uint8_t>(index)];
    entry.buffer = nullptr;
    entry.texture = texture;
    entry.sampler = nullptr;
    entry.offset = 0;
    entry.size = 0;
    dirty_mask_ |= static_cast<uint16_t>(uint16_t{ 1 } << static_cast<uint8_t>(index));
    return true;
}

bool DX11BindGroup::updateSampler(uint32_t binding, Sampler* sampler) {
    const int index = findEntry(binding);
    if (index < 0)
        return false;

    auto& entry = entries_[static_cast<uint8_t>(index)];
    entry.buffer = nullptr;
    entry.texture = nullptr;
    entry.sampler = sampler;
    entry.offset = 0;
    entry.size = 0;
    dirty_mask_ |= static_cast<uint16_t>(uint16_t{ 1 } << static_cast<uint8_t>(index));
    return true;
}

}  // namespace mulan::engine
