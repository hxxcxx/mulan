#include "bind_group.h"

#include "buffer.h"

#include <algorithm>
#include <limits>

namespace mulan::engine {
namespace {

const BindGroupLayoutEntry* findLayoutEntry(const BindGroupLayout& layout, uint32_t binding) {
    const auto& entries = layout.entries();
    const auto it =
            std::lower_bound(entries.begin(), entries.end(), binding,
                             [](const BindGroupLayoutEntry& entry, uint32_t value) { return entry.binding < value; });
    return it != entries.end() && it->binding == binding ? &*it : nullptr;
}

bool containsOnlyExpectedResource(const BindGroupEntry& entry) {
    switch (entry.type) {
    case DescriptorType::UniformBuffer: return entry.buffer && !entry.texture && !entry.sampler;
    case DescriptorType::TextureSRV: return !entry.buffer && entry.texture && !entry.sampler;
    case DescriptorType::Sampler: return !entry.buffer && !entry.texture && entry.sampler;
    }
    return false;
}

}  // namespace

std::string validateBindGroupDesc(const BindGroupLayout& layout, const BindGroupDesc& desc,
                                  const BindGroupValidationLimits& limits) {
    if (desc.count > BindGroupDesc::kMaxEntries)
        return "BindGroup entry count exceeds the RHI limit";
    if (layout.entries().size() != desc.count)
        return "BindGroup must provide exactly one entry for every layout binding";
    if (limits.minUniformBufferOffsetAlignment == 0)
        return "uniform-buffer offset alignment capability must not be zero";

    for (uint8_t i = 0; i < desc.count; ++i) {
        const auto& entry = desc.entries[i];
        const auto* layoutEntry = findLayoutEntry(layout, entry.binding);
        if (!layoutEntry)
            return "BindGroup contains a binding absent from its layout";
        if (layoutEntry->count != 1)
            return "descriptor arrays are not represented by the current RHI BindGroupEntry";
        if (entry.type != layoutEntry->type)
            return "BindGroup entry type does not match its layout binding";
        if (!containsOnlyExpectedResource(entry))
            return "BindGroup entry does not contain exactly the resource declared by its type";
        for (uint8_t j = 0; j < i; ++j) {
            if (desc.entries[j].binding == entry.binding)
                return "BindGroup contains duplicate bindings";
        }

        if (entry.type != DescriptorType::UniformBuffer)
            continue;
        if (!(entry.buffer->bindFlags() & BufferBindFlags::UniformBuffer))
            return "uniform-buffer binding requires BufferBindFlags::UniformBuffer";
        if (entry.offset > entry.buffer->size())
            return "uniform-buffer offset exceeds the buffer size";
        const uint32_t range = entry.size ? entry.size : entry.buffer->size() - entry.offset;
        if (range == 0)
            return "uniform-buffer binding range must not be empty";
        if (range > entry.buffer->size() - entry.offset)
            return "uniform-buffer binding range exceeds the buffer size";
        if ((entry.offset % limits.minUniformBufferOffsetAlignment) != 0)
            return "uniform-buffer offset does not satisfy the device alignment";
        if (range > limits.maxUniformBufferBindingSize)
            return "uniform-buffer binding range exceeds the device limit";
    }

    return {};
}

}  // namespace mulan::engine
