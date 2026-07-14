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

std::string validateUniformRange(const Buffer& buffer, uint32_t offset, uint32_t size,
                                 const BindGroupValidationLimits& limits) {
    if (!(buffer.bindFlags() & BufferBindFlags::UniformBuffer))
        return "uniform-buffer binding requires BufferBindFlags::UniformBuffer";
    if (offset > buffer.size())
        return "uniform-buffer offset exceeds the buffer size";
    const uint32_t range = size ? size : buffer.size() - offset;
    if (range == 0)
        return "uniform-buffer binding range must not be empty";
    if (range > buffer.size() - offset)
        return "uniform-buffer binding range exceeds the buffer size";
    if ((offset % limits.minUniformBufferOffsetAlignment) != 0)
        return "uniform-buffer offset does not satisfy the device alignment";
    if (range > limits.maxUniformBufferBindingSize)
        return "uniform-buffer binding range exceeds the device limit";
    return {};
}

}  // namespace

std::string validateBindGroupDesc(const BindGroupLayout& layout, const BindGroupDesc& desc,
                                  const BindGroupValidationLimits& limits) {
    if (desc.overflowed())
        return "BindGroup entry count exceeds the RHI limit";
    if (desc.count > BindGroupDesc::kMaxEntries)
        return "BindGroup entry count exceeds the RHI limit";
    if (limits.minUniformBufferOffsetAlignment == 0)
        return "uniform-buffer offset alignment capability must not be zero";

    for (const auto& entry : layout.entries()) {
        if (entry.count != 1)
            return "descriptor arrays are not represented by the current RHI BindGroupEntry";
        if (entry.mode != BindingMode::Dynamic)
            continue;
        if (entry.type != DescriptorType::UniformBuffer)
            return "dynamic bindings are restricted to UniformBuffer descriptors";
        if (entry.count != 1)
            return "dynamic UniformBuffer arrays are not supported";
    }

    const auto staticBindingCount = static_cast<size_t>(
            std::count_if(layout.entries().begin(), layout.entries().end(),
                          [](const BindGroupLayoutEntry& entry) { return entry.mode == BindingMode::Static; }));
    if (staticBindingCount != desc.count)
        return "BindGroup must provide exactly one entry for every static layout binding";

    for (uint8_t i = 0; i < desc.count; ++i) {
        const auto& entry = desc.entries[i];
        const auto* layoutEntry = findLayoutEntry(layout, entry.binding);
        if (!layoutEntry)
            return "BindGroup contains a binding absent from its layout";
        if (layoutEntry->mode != BindingMode::Static)
            return "dynamic uniform bindings must be supplied when the BindGroup is bound";
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
        if (auto error = validateUniformRange(*entry.buffer, entry.offset, entry.size, limits); !error.empty())
            return error;
    }

    return {};
}

std::string validateDynamicUniformBindings(const BindGroupLayout& layout,
                                           std::span<const DynamicUniformBinding> bindings,
                                           const BindGroupValidationLimits& limits, uint64_t recordingGeneration) {
    if (limits.minUniformBufferOffsetAlignment == 0)
        return "uniform-buffer offset alignment capability must not be zero";

    const auto dynamicBindingCount = static_cast<size_t>(
            std::count_if(layout.entries().begin(), layout.entries().end(),
                          [](const BindGroupLayoutEntry& entry) { return entry.mode == BindingMode::Dynamic; }));
    if (dynamicBindingCount != bindings.size())
        return "every dynamic layout binding must receive exactly one UniformSlice";

    for (size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        const auto* layoutEntry = findLayoutEntry(layout, binding.binding);
        if (!layoutEntry || layoutEntry->mode != BindingMode::Dynamic ||
            layoutEntry->type != DescriptorType::UniformBuffer) {
            return "dynamic binding does not match a dynamic UniformBuffer layout entry";
        }
        if (layoutEntry->count != 1)
            return "dynamic UniformBuffer arrays are not supported";
        if (!binding.slice)
            return "dynamic UniformBuffer binding contains an invalid UniformSlice";
        if (recordingGeneration != 0 && binding.slice.recordingGeneration != recordingGeneration)
            return "UniformSlice belongs to a different command recording";
        for (size_t j = 0; j < i; ++j) {
            if (bindings[j].binding == binding.binding)
                return "dynamic UniformBuffer bindings contain a duplicate binding";
        }

        if (auto error = validateUniformRange(*binding.slice.backingBuffer, binding.slice.offset, binding.slice.size,
                                              limits);
            !error.empty()) {
            return error;
        }
    }

    return {};
}

}  // namespace mulan::engine
