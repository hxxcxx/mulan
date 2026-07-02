/**
 * @file vertex_buffer.h
 * @brief 运行时顶点数据访问与构建工具
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "vertex_layout.h"

#include <cstring>
#include <vector>
#include <cstdint>

namespace mulan::engine {

// ============================================================
// 颜色打包工具
// ============================================================

// 将 RGBA [0-255] 打包为 uint32_t
constexpr uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

// 将 RGBA [0.0-1.0] 打包为 uint32_t
constexpr uint32_t packColorF(float r, float g, float b, float a = 1.0f) {
    return packColor(
        static_cast<uint8_t>(r * 255.0f),
        static_cast<uint8_t>(g * 255.0f),
        static_cast<uint8_t>(b * 255.0f),
        static_cast<uint8_t>(a * 255.0f)
    );
}

// ============================================================
// 顶点元素（单个属性的类型化视图）
// ============================================================

template<VertexFormat F>
struct VertexElement {
    static constexpr VertexFormat format = F;
    static constexpr uint8_t byteSize = vertexFormatSize(F);

    // 原始字节，4 字节对齐
    alignas(4) std::byte data_[byteSize];

    using value_type = typename VertexFormatTraits<F>::type;

    constexpr value_type get() const {
        value_type result;
        std::memcpy(&result, data_, sizeof(result));
        return result;
    }

    constexpr void set(const value_type& v) {
        std::memcpy(data_, &v, sizeof(v));
    }
};

// ============================================================
// 顶点缓冲视图（只读访问器）
// ============================================================

class VertexBufferView {
public:
    constexpr VertexBufferView(std::span<const std::byte> data,
                               const VertexLayout& layout)
        : data_(data), layout_(layout) {}

    constexpr uint32_t vertexCount() const {
        return layout_.stride() > 0
            ? static_cast<uint32_t>(data_.size()) / layout_.stride()
            : 0;
    }

    // 获取指定顶点属性的原始指针
    constexpr const std::byte* attributeData(uint32_t vertexIndex,
                                              VertexSemantic sem) const {
        auto* attr = layout_.find(sem);
        if (!attr) return nullptr;
        auto offset = vertexIndex * layout_.stride() + attr->offset;
        if (offset + attr->size() > data_.size()) return nullptr;
        return data_.data() + offset;
    }

    // 类型化读取（返回值拷贝）
    template<typename T>
    constexpr T read(uint32_t vertexIndex, VertexSemantic sem) const {
        auto* ptr = attributeData(vertexIndex, sem);
        if (!ptr) return T{};
        T result;
        std::memcpy(&result, ptr, sizeof(T));
        return result;
    }

    // 快捷：读取 float3 位置
    struct Float3 { float x, y, z; };
    Float3 readPosition(uint32_t idx) const {
        return read<Float3>(idx, VertexSemantic::Position);
    }

    // 快捷：读取打包颜色
    uint32_t readColor(uint32_t idx) const {
        return read<uint32_t>(idx, VertexSemantic::Color0);
    }

private:
    std::span<const std::byte> data_;
    VertexLayout layout_;
};

// ============================================================
// 顶点缓冲构建器（可变）
// ============================================================

class VertexBufferBuilder {
public:
    VertexBufferBuilder(const VertexLayout& layout, uint32_t maxVertices)
        : layout_(layout), max_vertices_(maxVertices)
    {
        buffer_.resize(static_cast<size_t>(maxVertices) * layout.stride());
    }

    uint32_t stride() const { return layout_.stride(); }
    uint32_t capacity() const { return max_vertices_; }

    // 类型化写入
    template<typename T>
    void write(uint32_t vertexIndex, VertexSemantic sem, const T& value) {
        auto* attr = layout_.find(sem);
        if (!attr) return;
        auto offset = vertexIndex * layout_.stride() + attr->offset;
        std::memcpy(buffer_.data() + offset, &value, sizeof(T));
    }

    void setPosition(uint32_t idx, float x, float y, float z) {
        float pos[3] = {x, y, z};
        write(idx, VertexSemantic::Position, pos);
    }

    void setNormal(uint32_t idx, float x, float y, float z) {
        float n[3] = {x, y, z};
        write(idx, VertexSemantic::Normal, n);
    }

    void setColor(uint32_t idx, uint32_t packedRGBA) {
        write(idx, VertexSemantic::Color0, packedRGBA);
    }

    void setColor(uint32_t idx, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        setColor(idx, packColor(r, g, b, a));
    }

    void setPickId(uint32_t idx, uint32_t id) {
        write(idx, VertexSemantic::PickId, id);
    }

    // 获取已构建的数据
    std::span<const std::byte> data() const {
        return {reinterpret_cast<const std::byte*>(buffer_.data()), buffer_.size()};
    }

    std::span<std::byte> mutableData() {
        return {reinterpret_cast<std::byte*>(buffer_.data()), buffer_.size()};
    }

    size_t sizeBytes() const { return buffer_.size(); }
    const VertexLayout& layout() const { return layout_; }

private:
    VertexLayout layout_;
    uint32_t max_vertices_;
    std::vector<std::byte> buffer_;
};

} // namespace mulan::engine
