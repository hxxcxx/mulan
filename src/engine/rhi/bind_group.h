/**
 * @file bind_group.h
 * @brief 资源绑定组 — 统一 UBO / Texture / Sampler 绑定接口
 * @author hxxcxx
 * @date 2026-04-25
 *
 * BindGroup 是值类型（stack 分配），帧内临时使用。
 * 类型由哪个指针非 null 推断：
 *   buffer  != null → UniformBuffer
 *   texture != null → Texture SRV
 *   sampler != null → Sampler
 */

#pragma once

#include <cstdint>

namespace mulan::engine {

class Buffer;
class Texture;
class Sampler;

struct BindGroupEntry {
    uint32_t binding = 0;
    Buffer*  buffer  = nullptr;    // UBO 时非 null
    Texture* texture = nullptr;    // Texture SRV 时非 null
    Sampler* sampler = nullptr;    // Sampler 时非 null
    uint32_t offset  = 0;          // UBO offset
    uint32_t size    = 0;          // UBO size
};

struct BindGroup {
    static constexpr uint8_t kMaxEntries = 16;
    BindGroupEntry entries[kMaxEntries]{};
    uint8_t count = 0;

    BindGroup& addUBO(uint32_t binding, Buffer* buf,
                      uint32_t offset, uint32_t size) noexcept {
        if (count < kMaxEntries) {
            entries[count++] = {binding, buf, nullptr, nullptr, offset, size};
        }
        return *this;
    }

    BindGroup& addTexture(uint32_t binding, Texture* tex) noexcept {
        if (count < kMaxEntries) {
            entries[count++] = {binding, nullptr, tex, nullptr, 0, 0};
        }
        return *this;
    }

    BindGroup& addSampler(uint32_t binding, Sampler* s) noexcept {
        if (count < kMaxEntries) {
            entries[count++] = {binding, nullptr, nullptr, s, 0, 0};
        }
        return *this;
    }

    void clear() noexcept { count = 0; }
};

} // namespace mulan::engine
