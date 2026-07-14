#include "bind_group_layout.h"

#include <algorithm>

namespace mulan::engine {

BindGroupLayout BindGroupLayout::fromPipelineDesc(const GraphicsPipelineDesc& desc) {
    std::vector<BindGroupLayoutEntry> entries;
    entries.reserve(desc.descriptorBindingCount);

    for (uint8_t i = 0; i < desc.descriptorBindingCount; ++i) {
        const auto& b = desc.descriptorBindings[i];
        if (b.count == 0)
            continue;
        entries.push_back({ b.binding, b.count, b.type, b.stages, b.mode });
    }

    // 按 binding 排序，确保布局等价判定稳定
    std::sort(entries.begin(), entries.end(),
              [](const BindGroupLayoutEntry& a, const BindGroupLayoutEntry& b) { return a.binding < b.binding; });

    const uint64_t hash = computeHash(entries);
    return BindGroupLayout(std::move(entries), hash);
}

BindGroupLayout BindGroupLayout::fromPipelineDesc(const ComputePipelineDesc& desc) {
    std::vector<BindGroupLayoutEntry> entries;
    entries.reserve(desc.descriptorBindingCount);
    for (uint8_t i = 0; i < desc.descriptorBindingCount; ++i) {
        const auto& binding = desc.descriptorBindings[i];
        if (binding.count > 0)
            entries.push_back({ binding.binding, binding.count, binding.type, binding.stages, binding.mode });
    }
    std::sort(entries.begin(), entries.end(),
              [](const BindGroupLayoutEntry& a, const BindGroupLayoutEntry& b) { return a.binding < b.binding; });
    const uint64_t hash = computeHash(entries);
    return BindGroupLayout(std::move(entries), hash);
}

BindGroupLayout BindGroupLayout::fromBindings(std::span<const BindGroupLayoutEntry> entries) {
    std::vector<BindGroupLayoutEntry> sorted(entries.begin(), entries.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const BindGroupLayoutEntry& a, const BindGroupLayoutEntry& b) { return a.binding < b.binding; });
    const uint64_t hash = computeHash(sorted);
    return BindGroupLayout(std::move(sorted), hash);
}

uint64_t BindGroupLayout::computeHash(const std::vector<BindGroupLayoutEntry>& entries) {
    // FNV-1a 64-bit hash over binding entries
    constexpr uint64_t fnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t fnvPrime = 1099511628211ULL;
    uint64_t hash = fnvOffsetBasis;

    auto hashByte = [&hash](uint8_t b) {
        hash ^= b;
        hash *= fnvPrime;
    };

    auto hash32 = [&hashByte](uint32_t v) {
        hashByte(static_cast<uint8_t>(v));
        hashByte(static_cast<uint8_t>(v >> 8));
        hashByte(static_cast<uint8_t>(v >> 16));
        hashByte(static_cast<uint8_t>(v >> 24));
    };

    for (const auto& e : entries) {
        hash32(e.binding);
        hash32(e.count);
        hashByte(static_cast<uint8_t>(e.type));
        hashByte(static_cast<uint8_t>(e.mode));
        hash32(e.stages);
    }

    return hash;
}

const BindGroupLayout& BindGroupLayout::empty() {
    static const BindGroupLayout s_empty({}, 0);
    return s_empty;
}

// ============================================================
// PipelineState::bindGroupLayout() 实现（需 BindGroupLayout 完整定义）
// ============================================================

PipelineState::PipelineState() = default;
PipelineState::~PipelineState() = default;

const BindGroupLayout& PipelineState::bindGroupLayout() const {
    if (!bg_layout_) {
        bg_layout_ = std::make_unique<BindGroupLayout>(BindGroupLayout::fromPipelineDesc(desc()));
    }
    return *bg_layout_;
}

ComputePipelineState::ComputePipelineState() = default;
ComputePipelineState::~ComputePipelineState() = default;

const BindGroupLayout& ComputePipelineState::bindGroupLayout() const {
    if (!bg_layout_)
        bg_layout_ = std::make_unique<BindGroupLayout>(BindGroupLayout::fromPipelineDesc(desc()));
    return *bg_layout_;
}

}  // namespace mulan::engine
