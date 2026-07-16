#include "detail/dx12_bind_group.h"
#include "../rhi/buffer.h"
#include "../rhi/texture.h"
#include "../rhi/sampler.h"

namespace mulan::engine {

DX12BindGroup::DX12BindGroup(const BindGroupLayout& layout, const BindGroupEntry* entries, uint8_t count,
                             BindGroupValidationLimits limits)
    : BindGroup(limits), layout_(layout), count_(count) {
    for (uint8_t i = 0; i < count; ++i)
        entries_[i] = entries[i];

    // 计算 binding → root parameter index 映射。
    // 规则（与 DX12PipelineState::createRootSignature 一致）：
    //   按 layout entries（已按 binding 排序）遍历，所有 descriptor 类型依次
    //   分配 root index 0,1,2,...；count==0 的条目在 layout 阶段已被过滤。
    uint32_t nextRoot = 0;
    for (const auto& e : layout.entries()) {
        root_map_[root_map_count_++] = { e.binding, nextRoot++ };
    }
}

uint32_t DX12BindGroup::rootIndexForBinding(uint32_t binding) const {
    for (uint8_t i = 0; i < root_map_count_; ++i) {
        if (root_map_[i].binding == binding)
            return root_map_[i].rootIndex;
    }
    return kInvalidRootIndex;
}

bool DX12BindGroup::updateUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].binding == binding) {
            if (entries_[i].type != DescriptorType::UniformBuffer)
                return false;
            if (!validateUniformUpdate(buf, offset, size))
                return false;
            if (entries_[i].buffer == buf && entries_[i].offset == offset && entries_[i].size == size &&
                !entries_[i].texture && !entries_[i].sampler)
                return true;
            entries_[i].buffer = buf;
            entries_[i].texture = nullptr;
            entries_[i].sampler = nullptr;
            entries_[i].offset = offset;
            entries_[i].size = size;
            dirty_mask_ |= (uint16_t(1) << i);
            return true;
        }
    }
    return false;
}

bool DX12BindGroup::updateTexture(uint32_t binding, Texture* tex) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].binding == binding) {
            if (entries_[i].type != DescriptorType::TextureSRV)
                return false;
            if (!validateResourceUpdate(tex))
                return false;
            if (entries_[i].texture == tex && !entries_[i].buffer && !entries_[i].sampler)
                return true;
            entries_[i].texture = tex;
            entries_[i].buffer = nullptr;
            entries_[i].sampler = nullptr;
            dirty_mask_ |= (uint16_t(1) << i);
            return true;
        }
    }
    return false;
}

bool DX12BindGroup::updateSampler(uint32_t binding, Sampler* s) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].binding == binding) {
            if (entries_[i].type != DescriptorType::Sampler)
                return false;
            if (!validateResourceUpdate(s))
                return false;
            if (entries_[i].sampler == s && !entries_[i].buffer && !entries_[i].texture)
                return true;
            entries_[i].sampler = s;
            entries_[i].buffer = nullptr;
            entries_[i].texture = nullptr;
            dirty_mask_ |= (uint16_t(1) << i);
            return true;
        }
    }
    return false;
}

}  // namespace mulan::engine
