#include "detail/dx12_bind_group.h"
#include "../rhi/buffer.h"
#include "../rhi/texture.h"
#include "../rhi/sampler.h"

namespace mulan::engine {

DX12BindGroup::DX12BindGroup(const BindGroupLayout& layout, const BindGroupEntry* entries, uint8_t count)
    : layout_(&layout), count_(count) {
    for (uint8_t i = 0; i < count; ++i)
        entries_[i] = entries[i];

    // 计算 binding → root parameter index 映射。
    // 规则（与 DX12PipelineState::createRootSignature 一致）：
    //   按 layout entries（已按 binding 排序）遍历，Sampler 不占 root slot，
    //   其余类型依次分配 root index 0,1,2,...
    //   count==0 的条目在 fromPipelineDesc 阶段已被过滤，这里无需再判。
    uint32_t nextRoot = 0;
    for (const auto& e : layout.entries()) {
        uint32_t idx = (e.type == DescriptorType::Sampler) ? kInvalidRootIndex : nextRoot++;
        root_map_[root_map_count_++] = { e.binding, idx };
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
            entries_[i].buffer = buf;
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
            entries_[i].texture = tex;
            dirty_mask_ |= (uint16_t(1) << i);
            return true;
        }
    }
    return false;
}

bool DX12BindGroup::updateSampler(uint32_t binding, Sampler* s) {
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
