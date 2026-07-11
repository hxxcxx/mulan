#include "vk_bind_group.h"
#include "../rhi/buffer.h"
#include "../rhi/texture.h"
#include "../rhi/sampler.h"

namespace mulan::engine {

VKBindGroup::VKBindGroup(const BindGroupLayout& layout, const BindGroupEntry* entries, uint8_t count)
    : layout_(&layout), count_(count) {
    for (uint8_t i = 0; i < count; ++i)
        entries_[i] = entries[i];
}

bool VKBindGroup::updateUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].binding == binding) {
            entries_[i].buffer = buf;
            entries_[i].offset = offset;
            entries_[i].size = size;
            dirty_mask_ |= (uint16_t(1) << i);
            return true;
        }
    }
    return false;
}

bool VKBindGroup::updateTexture(uint32_t binding, Texture* tex) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].binding == binding) {
            entries_[i].texture = tex;
            dirty_mask_ |= (uint16_t(1) << i);
            return true;
        }
    }
    return false;
}

bool VKBindGroup::updateSampler(uint32_t binding, Sampler* s) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].binding == binding) {
            entries_[i].sampler = s;
            dirty_mask_ |= (uint16_t(1) << i);
            return true;
        }
    }
    return false;
}

}  // namespace mulan::engine
