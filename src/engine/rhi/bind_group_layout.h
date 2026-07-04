/**
 * @file bind_group_layout.h
 * @brief BindGroupLayout —— PSO 与 BindGroup 之间的契约，描述一组 descriptor binding 声明
 * @author hxxcxx
 * @date 2026-07-04
 *
 * BindGroupLayout 从 GraphicsPipelineDesc::descriptorBindings 派生，不可变。
 * 两个 PSO 若有完全相同的 binding 声明，则共享同一个 layout（通过哈希去重）。
 *
 * 设计：
 *  - 值语义（轻量拷贝），但只通过 Device 工厂创建
 *  - 内部包含稳定排序后的 binding 列表 + 预计算哈希
 *  - BindGroup 必须从 layout 创建，layout 即 PSO 的 binding 契约
 */

#pragma once

#include "pipeline_state.h"  // DescriptorType, PipelineBinding

#include <cstdint>
#include <span>
#include <vector>

namespace mulan::engine {

struct BindGroupLayoutEntry {
    uint32_t       binding = 0;
    uint32_t       count   = 1;
    DescriptorType type    = DescriptorType::UniformBuffer;
    uint32_t       stages  = PipelineBinding::kStageAll;
};

class BindGroupLayout {
public:
    BindGroupLayout() = default;

    /// 从 PSO 描述符绑定构建 layout（自动按 binding 排序 + 哈希）
    static BindGroupLayout fromPipelineDesc(const GraphicsPipelineDesc& desc);

    /// 直接构造（用于测试 / 手工创建）
    static BindGroupLayout fromBindings(std::span<const BindGroupLayoutEntry> entries);

    const std::vector<BindGroupLayoutEntry>& entries() const { return entries_; }
    uint64_t hash() const { return hash_; }

    bool operator==(const BindGroupLayout& other) const { return hash_ == other.hash_; }
    bool operator!=(const BindGroupLayout& other) const { return hash_ != other.hash_; }

    /// 空 layout（用于默认构造的 BindGroup，无缓存能力）
    static const BindGroupLayout& empty();

private:
    BindGroupLayout(std::vector<BindGroupLayoutEntry> entries, uint64_t hash)
        : entries_(std::move(entries)), hash_(hash) {}

    static uint64_t computeHash(const std::vector<BindGroupLayoutEntry>& entries);

    std::vector<BindGroupLayoutEntry> entries_;
    uint64_t hash_ = 0;
};

} // namespace mulan::engine

// std::hash 特化，用于 unordered_map 缓存
namespace std {
template<>
struct hash<mulan::engine::BindGroupLayout> {
    size_t operator()(const mulan::engine::BindGroupLayout& l) const {
        return static_cast<size_t>(l.hash());
    }
};
} // namespace std
