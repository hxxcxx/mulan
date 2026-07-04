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

#include <array>
#include <cstdint>

namespace mulan::engine {

class Buffer;
class Texture;
class Sampler;

// ============================================================
// BindGroupEntry — 单个 binding 槽的值描述
// ============================================================

struct BindGroupEntry {
    uint32_t binding = 0;
    Buffer*  buffer  = nullptr;
    Texture* texture = nullptr;
    Sampler* sampler = nullptr;
    uint32_t offset  = 0;
    uint32_t size    = 0;
};

// ============================================================
// BindGroupDesc — 值类型构建器（临时栈分配，传给工厂或便捷绑定）
// ============================================================

struct BindGroupDesc {
    static constexpr uint8_t kMaxEntries = 16;
    BindGroupEntry entries[kMaxEntries]{};
    uint8_t count = 0;

    BindGroupDesc& addUBO(uint32_t binding, Buffer* buf,
                          uint32_t offset, uint32_t size) noexcept {
        if (count < kMaxEntries)
            entries[count++] = {binding, buf, nullptr, nullptr, offset, size};
        return *this;
    }

    BindGroupDesc& addTexture(uint32_t binding, Texture* tex) noexcept {
        if (count < kMaxEntries)
            entries[count++] = {binding, nullptr, tex, nullptr, 0, 0};
        return *this;
    }

    BindGroupDesc& addSampler(uint32_t binding, Sampler* s) noexcept {
        if (count < kMaxEntries)
            entries[count++] = {binding, nullptr, nullptr, s, 0, 0};
        return *this;
    }

    void clear() noexcept { count = 0; }
};

// ============================================================
// BindGroup — 抽象基类（后端实现 VKBindGroup / DX12BindGroup）
// ============================================================

class BindGroup {
public:
    virtual ~BindGroup() = default;

    virtual const BindGroupLayout& layout() const = 0;
    virtual const BindGroupEntry*  entries() const = 0;
    virtual uint8_t                entryCount() const = 0;

    /// 更新已有 binding 的 UBO offset/size（标脏，下次 bind 重新写入）
    virtual bool updateUBO(uint32_t binding, Buffer* buf,
                           uint32_t offset, uint32_t size) = 0;
    virtual bool updateTexture(uint32_t binding, Texture* tex) { (void)binding; (void)tex; return false; }
    virtual bool updateSampler(uint32_t binding, Sampler* s) { (void)binding; (void)s; return false; }

    virtual bool dirty() const = 0;
    virtual void markClean() = 0;

protected:
    BindGroup() = default;
    BindGroup(const BindGroup&) = delete;
    BindGroup& operator=(const BindGroup&) = delete;
};

} // namespace mulan::engine
