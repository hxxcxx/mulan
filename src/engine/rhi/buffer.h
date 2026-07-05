/**
 * @file buffer.h
 * @brief GPU缓冲区资源描述与接口定义
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace mulan::engine {

// ============================================================
// 缓冲区用途
// ============================================================

enum class BufferUsage : uint8_t {
    Immutable,  // 一次性上传，GPU 只读（相当于 D3D USAGE_IMMUTABLE）
    Default,    // GPU 读写，偶尔 CPU 更新（通过 UpdateBuffer）
    Dynamic,    // 频繁 CPU→GPU 更新（每帧/每几帧）
    Staging,    // CPU 可读写，用于数据搬移
};

// ============================================================
// 缓冲区绑定标志（位掩码）
// ============================================================

enum class BufferBindFlags : uint32_t {
    None = 0,
    VertexBuffer = 1 << 0,
    IndexBuffer = 1 << 1,
    UniformBuffer = 1 << 2,
    ShaderResource = 1 << 3,   // SRV / SSBO
    UnorderedAccess = 1 << 4,  // UAV / image load store
    IndirectBuffer = 1 << 5,   // 间接绘制参数
};

constexpr BufferBindFlags operator|(BufferBindFlags a, BufferBindFlags b) {
    return static_cast<BufferBindFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr bool operator&(BufferBindFlags a, BufferBindFlags b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// ============================================================
// 缓冲区描述结构体
// ============================================================

struct BufferDesc {
    std::string_view name;  // 调试名称
    uint32_t size = 0;      // 字节大小
    BufferUsage usage = BufferUsage::Immutable;
    BufferBindFlags bindFlags = BufferBindFlags::None;
    const void* initData = nullptr;  // 可选：初始数据

    // 便捷构造

    static BufferDesc vertex(uint32_t size, const void* data = nullptr, std::string_view debugName = {}) {
        return { debugName, size, BufferUsage::Immutable, BufferBindFlags::VertexBuffer, data };
    }

    static BufferDesc index(uint32_t size, const void* data = nullptr, std::string_view debugName = {}) {
        return { debugName, size, BufferUsage::Immutable, BufferBindFlags::IndexBuffer, data };
    }

    static BufferDesc uniform(uint32_t size, std::string_view debugName = {}) {
        return { debugName, size, BufferUsage::Dynamic, BufferBindFlags::UniformBuffer, nullptr };
    }

    static BufferDesc dynamicVertex(uint32_t size, std::string_view debugName = {}) {
        return { debugName, size, BufferUsage::Dynamic, BufferBindFlags::VertexBuffer, nullptr };
    }

    static BufferDesc staging(uint32_t size, std::string_view debugName = {}) {
        return { debugName, size, BufferUsage::Staging, BufferBindFlags::None, nullptr };
    }
};

// ============================================================
// GPU 缓冲区基类
//
// 纯数据容器：只存储描述信息和平台相关句柄。
// 不提供 bind()、update() 等操作 — 由 CommandList 负责。
// ============================================================

class Buffer {
public:
    virtual ~Buffer() = default;

    virtual const BufferDesc& desc() const = 0;

    /// CPU 端更新缓冲区数据（对于 UBO/Dynamic buffer 直接映射写入）
    virtual void update(uint32_t offset, uint32_t size, const void* data) = 0;

    /// CPU 端回读缓冲区数据（仅 Staging buffer 支持）
    virtual bool readback(uint32_t offset, uint32_t size, void* outData) {
        (void) offset;
        (void) size;
        (void) outData;
        return false;
    }

    // 便捷查询
    uint32_t size() const { return desc().size; }
    BufferUsage usage() const { return desc().usage; }
    BufferBindFlags bindFlags() const { return desc().bindFlags; }

protected:
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

}  // namespace mulan::engine
