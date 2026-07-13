/**
 * @file bind_group.h
 * @brief BindGroup 抽象基类 + BindGroupDesc 值类型构建器
 * @author hxxcxx
 * @date 2026-04-25 (原始) / 2026-07-04 (抽象基类重构)
 *
 * 架构：
 *  - BindGroup（抽象基类）—— 与 Buffer/Texture/Shader 同级的 RHI 资源，
 *    由 Device::createBindGroup() 创建，后端持有缓存的 descriptor set / heap 句柄。
 *  - BindGroupDesc（值类型）—— 临时构建器，addUBO/addTexture/addSampler，
 *    传给 Device 工厂或 CommandList::bindResources() 便捷路径。
 */

#pragma once

#include "bind_group_layout.h"
#include "resource.h"
#include "uniform_slice.h"

#include <array>
#include <cstdint>
#include <string>
#include <span>

namespace mulan::engine {

class Buffer;
class Texture;
class Sampler;

// ============================================================
// BindGroupEntry — 单个 binding 槽的值描述
// ============================================================

struct BindGroupEntry {
    uint32_t binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;
    Buffer* buffer = nullptr;
    Texture* texture = nullptr;
    Sampler* sampler = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;

    static BindGroupEntry uniformBuffer(uint32_t binding, Buffer* buffer, uint32_t offset, uint32_t size) noexcept {
        return { binding, DescriptorType::UniformBuffer, buffer, nullptr, nullptr, offset, size };
    }

    static BindGroupEntry textureSRV(uint32_t binding, Texture* texture) noexcept {
        return { binding, DescriptorType::TextureSRV, nullptr, texture, nullptr, 0, 0 };
    }

    static BindGroupEntry samplerResource(uint32_t binding, Sampler* sampler) noexcept {
        return { binding, DescriptorType::Sampler, nullptr, nullptr, sampler, 0, 0 };
    }
};

// ============================================================
// BindGroupDesc — 值类型构建器（临时栈分配，传给工厂或便捷绑定）
// ============================================================

struct BindGroupDesc {
    static constexpr uint8_t kMaxEntries = 16;
    BindGroupEntry entries[kMaxEntries]{};
    uint8_t count = 0;

    BindGroupDesc& addUniformBuffer(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) noexcept {
        if (count < kMaxEntries)
            entries[count++] = BindGroupEntry::uniformBuffer(binding, buf, offset, size);
        return *this;
    }

    BindGroupDesc& addUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) noexcept {
        return addUniformBuffer(binding, buf, offset, size);
    }

    BindGroupDesc& addTexture(uint32_t binding, Texture* tex) noexcept {
        if (count < kMaxEntries)
            entries[count++] = BindGroupEntry::textureSRV(binding, tex);
        return *this;
    }

    BindGroupDesc& addSampler(uint32_t binding, Sampler* s) noexcept {
        if (count < kMaxEntries)
            entries[count++] = BindGroupEntry::samplerResource(binding, s);
        return *this;
    }

    void clear() noexcept { count = 0; }
};

struct BindGroupValidationLimits {
    uint32_t minUniformBufferOffsetAlignment = 1;
    uint32_t maxUniformBufferBindingSize = UINT32_MAX;
};

/// Validates the complete RHI binding contract before a backend creates native descriptors.
/// An empty string means the descriptor is valid.
std::string validateBindGroupDesc(const BindGroupLayout& layout, const BindGroupDesc& desc,
                                  const BindGroupValidationLimits& limits);

/// Validates the dynamic UniformBuffer bindings supplied when a BindGroup is bound.
std::string validateDynamicUniformBindings(const BindGroupLayout& layout,
                                           std::span<const DynamicUniformBinding> bindings,
                                           const BindGroupValidationLimits& limits, uint64_t recordingGeneration = 0);

// ============================================================
// BindGroup — 抽象基类（后端实现 VKBindGroup / DX12BindGroup）
// ============================================================

class BindGroup : public RHITrackedResource {
public:
    virtual ~BindGroup() = default;

    virtual const BindGroupLayout& layout() const = 0;
    virtual const BindGroupEntry* entries() const = 0;
    virtual uint8_t entryCount() const = 0;

    /// 更新已有 binding 的 UBO offset/size（标脏，下次 bind 重新写入）
    virtual bool updateUBO(uint32_t binding, Buffer* buf, uint32_t offset, uint32_t size) = 0;
    virtual bool updateTexture(uint32_t binding, Texture* tex) {
        (void) binding;
        (void) tex;
        return false;
    }
    virtual bool updateSampler(uint32_t binding, Sampler* s) {
        (void) binding;
        (void) s;
        return false;
    }

    /// 是否有任意 binding 处于脏状态。
    bool dirty() const { return dirty_mask_ != 0; }
    /// 全部清脏（整体重写后调用）。
    void markClean() { dirty_mask_ = 0; }
    /// 当前脏 binding 位掩码（位 i 对应 entries_[i]）。
    uint16_t dirtyMask() const { return dirty_mask_; }
    /// 清除指定位掩码对应的脏标记（局部重写后调用，只清已写入的位）。
    void clearDirty(uint16_t mask) { dirty_mask_ &= ~mask; }
    /// 标记所有 binding 脏（整体失效时调用，如首次 bind）。
    void markAllDirty() { dirty_mask_ = 0xFFFF; }

    // ─── Frame token（descriptor 句柄版本化）────────────────────
    // 缓存的 descriptor 句柄（VK VkDescriptorSet / DX12 GPU handle）所属的
    // frame token。后端 per-frame 会 reset descriptor pool/heap，使旧句柄失效；
    // 命令缓冲区 bindGroup 入口比对 token，跨帧即丢弃缓存句柄并 markAllDirty，
    // 同帧内复用命中（commit "对象化 BindGroup" 的优化本意）。
    // token 单调递增不复用；0 表示无效（未分配过句柄）。
    static constexpr uint64_t kInvalidFrameToken = 0;

    uint64_t frameToken() const { return frame_token_; }
    void setFrameToken(uint64_t token) { frame_token_ = token; }

protected:
    BindGroup() = default;
    BindGroup(const BindGroup&) = delete;
    BindGroup& operator=(const BindGroup&) = delete;

    uint16_t dirty_mask_ = 0xFFFF;  // 初次 bind 视为整体脏，确保首帧完整写入
    uint64_t frame_token_ = kInvalidFrameToken;
};

}  // namespace mulan::engine
