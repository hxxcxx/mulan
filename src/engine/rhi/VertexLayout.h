/**
 * @file VertexLayout.h
 * @brief 顶点布局描述，定义顶点结构的完整内存布局
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "VertexFormat.h"
#include "VertexSemantic.h"

#include <array>
#include <span>
#include <cstdint>

namespace mulan::engine {

// ============================================================
// 顶点属性（布局中的单个属性）
// ============================================================

struct VertexAttribute {
    VertexSemantic semantic   = VertexSemantic::Position;
    VertexFormat   format     = VertexFormat::Invalid;
    uint16_t       offset     = 0;        // 距顶点起始的字节偏移
    uint8_t        bufferSlot = 0;        // vertex buffer slot [0-3]

    constexpr VertexAttribute() = default;
    constexpr VertexAttribute(VertexSemantic sem, VertexFormat fmt,
                              uint16_t off = 0, uint8_t slot = 0)
        : semantic(sem), format(fmt), offset(off), bufferSlot(slot) {}

    constexpr uint8_t size() const { return vertexFormatSize(format); }
    constexpr bool isValid() const { return format != VertexFormat::Invalid; }

    constexpr bool operator==(const VertexAttribute& o) const {
        return semantic == o.semantic && format == o.format
            && offset == o.offset && bufferSlot == o.bufferSlot;
    }
};

// ============================================================
// 顶点布局（完整描述一个顶点的结构）
// ============================================================

static constexpr uint8_t kMaxVertexAttributes = 16;
static constexpr uint8_t kMaxVertexBuffers    = 4;

class VertexLayout {
public:
    constexpr VertexLayout() = default;

    // 构建器模式（全部 constexpr）
    constexpr VertexLayout& begin(uint8_t bufferCount = 1) {
        m_attrCount   = 0;
        m_stride      = 0;
        m_bufferCount = bufferCount;
        return *this;
    }

    constexpr VertexLayout& add(VertexSemantic semantic, VertexFormat format,
                                uint8_t bufferSlot = 0) {
        if (m_attrCount < kMaxVertexAttributes) {
            m_attrs[m_attrCount] = VertexAttribute(semantic, format, m_stride, bufferSlot);
            m_stride += vertexFormatSize(format);
            ++m_attrCount;
        }
        return *this;
    }

    constexpr VertexLayout& add(const VertexAttribute& attr) {
        if (m_attrCount < kMaxVertexAttributes) {
            auto& a = m_attrs[m_attrCount];
            a = attr;
            a.offset = m_stride;
            ++m_attrCount;
            m_stride += a.size();
        }
        return *this;
    }

    // 跳过 N 字节（手动填充）
    constexpr VertexLayout& skip(uint8_t bytes) {
        m_stride += bytes;
        return *this;
    }

    constexpr VertexLayout& end() {
        // stride 4 字节对齐
        m_stride = (m_stride + 3u) & ~3u;
        return *this;
    }

    // 查询
    constexpr uint16_t stride() const { return m_stride; }
    constexpr uint8_t  attrCount() const { return m_attrCount; }
    constexpr uint8_t  bufferCount() const { return m_bufferCount; }
    constexpr bool     empty() const { return m_attrCount == 0; }

    constexpr const VertexAttribute& operator[](uint8_t i) const { return m_attrs[i]; }
    constexpr std::span<const VertexAttribute> attributes() const {
        return {m_attrs.data(), m_attrCount};
    }

    // 按语义查找属性
    constexpr const VertexAttribute* find(VertexSemantic sem) const {
        for (uint8_t i = 0; i < m_attrCount; ++i) {
            if (m_attrs[i].semantic == sem) return &m_attrs[i];
        }
        return nullptr;
    }

    constexpr bool has(VertexSemantic sem) const {
        return find(sem) != nullptr;
    }

    constexpr uint16_t offsetOf(VertexSemantic sem) const {
        auto* a = find(sem);
        return a ? a->offset : 0xFFFF;
    }

private:
    std::array<VertexAttribute, kMaxVertexAttributes> m_attrs{};
    uint16_t m_stride      = 0;
    uint8_t  m_attrCount   = 0;
    uint8_t  m_bufferCount = 1;
};

// ============================================================
// 索引类型
// ============================================================

enum class IndexType : uint8_t {
    UInt16 = 0,
    UInt32 = 1,
};

consteval uint8_t indexTypeSize(IndexType t) {
    return t == IndexType::UInt16 ? 2 : 4;
}

// ============================================================
// 预定义 CAD 顶点布局
//
// CAD 查看器所需的全部布局。
// 其他格式（STEP、glTF、DWG、IFC 等）在网格适配层
// 转换为以下布局之一。
// ============================================================

namespace layouts {

// 布局 A: Wireframe / edges
// position(f3) + color(u32 packed), 16 bytes
consteval VertexLayout wire() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position, VertexFormat::Float3)
        .add(VertexSemantic::Color0,   VertexFormat::UByte4N)
     .end();
    return l;
}

// 布局 B: Solid fill
// position(f3) + normal(f3) + color(u32 packed) + pad(4)
// 32 bytes, cache-line 友好
consteval VertexLayout solid() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position, VertexFormat::Float3)
        .add(VertexSemantic::Normal,   VertexFormat::Float3)
        .add(VertexSemantic::Color0,   VertexFormat::UByte4N)
        .skip(4)
     .end();
    return l;
}

// 布局 C: Solid + material ID
consteval VertexLayout solidMaterial() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position,   VertexFormat::Float3)
        .add(VertexSemantic::Normal,     VertexFormat::Float3)
        .add(VertexSemantic::Color0,     VertexFormat::UByte4N)
        .add(VertexSemantic::MaterialId, VertexFormat::UInt)
     .end();
    return l;
}

// 布局 D: Picking pass
consteval VertexLayout pick() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position, VertexFormat::Float3)
        .add(VertexSemantic::PickId,   VertexFormat::UInt)
     .end();
    return l;
}

// 布局 E: Full PBR
consteval VertexLayout pbr() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position,  VertexFormat::Float3)
        .add(VertexSemantic::Normal,    VertexFormat::Float3)
        .add(VertexSemantic::TexCoord0, VertexFormat::Float2)
        .add(VertexSemantic::Tangent,   VertexFormat::Float4)
     .end();
    return l;
}

// 布局 F: Point cloud
consteval VertexLayout pointCloud() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position, VertexFormat::Float3)
        .add(VertexSemantic::Color0,   VertexFormat::UByte4N)
     .end();
    return l;
}

// 布局 G: BIM/IFC，含 object 元数据
consteval VertexLayout bim() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position,   VertexFormat::Float3)
        .add(VertexSemantic::Normal,     VertexFormat::Float3)
        .add(VertexSemantic::Color0,     VertexFormat::UByte4N)
        .add(VertexSemantic::ObjectId,   VertexFormat::UInt)
        .add(VertexSemantic::MaterialId, VertexFormat::UInt)
        .skip(8)
     .end();
    return l;
}

// 布局 H: 2D overlay
consteval VertexLayout overlay2D() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position, VertexFormat::Float2)
        .add(VertexSemantic::Color0,   VertexFormat::UByte4N)
     .end();
    return l;
}

// 布局 I: SoA multi-buffer
consteval VertexLayout soaSolid() {
    VertexLayout l;
    l.begin(3)
        .add(VertexSemantic::Position, VertexFormat::Float3,  0)
        .add(VertexSemantic::Normal,   VertexFormat::Float3,  1)
        .add(VertexSemantic::Color0,   VertexFormat::UByte4N, 2)
     .end();
    return l;
}

// 布局 J: Skinned mesh
consteval VertexLayout skinned() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position,  VertexFormat::Float3)
        .add(VertexSemantic::Normal,    VertexFormat::Float3)
        .add(VertexSemantic::TexCoord0, VertexFormat::Float2)
        .add(VertexSemantic::Tangent,   VertexFormat::Float4)
        .add(VertexSemantic::Indices,   VertexFormat::UInt)
        .add(VertexSemantic::Weight,    VertexFormat::Float4)
     .end();
    return l;
}

// 布局 K: Solid + texture
consteval VertexLayout solidTextured() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position,  VertexFormat::Float3)
        .add(VertexSemantic::Normal,    VertexFormat::Float3)
        .add(VertexSemantic::Color0,    VertexFormat::UByte4N)
        .add(VertexSemantic::TexCoord0, VertexFormat::Float2)
        .skip(4)
     .end();
    return l;
}

// 布局 L: Lightmap / double-UV
consteval VertexLayout lightmap() {
    VertexLayout l;
    l.begin(1)
        .add(VertexSemantic::Position,  VertexFormat::Float3)
        .add(VertexSemantic::Normal,    VertexFormat::Float3)
        .add(VertexSemantic::TexCoord0, VertexFormat::Float2)
        .add(VertexSemantic::TexCoord1, VertexFormat::Float2)
        .add(VertexSemantic::Tangent,   VertexFormat::Float4)
     .end();
    return l;
}

} // namespace layouts

// ============================================================
// 编译期布局验证
// ============================================================

constexpr bool validateLayout(const VertexLayout& L) {
    for (uint8_t i = 0; i < L.attrCount(); ++i) {
        for (uint8_t j = i + 1; j < L.attrCount(); ++j) {
            if (L[i].semantic == L[j].semantic) return false;
        }
    }
    return true;
}

// 内置布局合法性检查
static_assert(validateLayout(layouts::wire()));
static_assert(validateLayout(layouts::solid()));
static_assert(validateLayout(layouts::pick()));
static_assert(validateLayout(layouts::pbr()));
static_assert(validateLayout(layouts::pointCloud()));
static_assert(validateLayout(layouts::bim()));
static_assert(validateLayout(layouts::overlay2D()));
static_assert(validateLayout(layouts::soaSolid()));
static_assert(validateLayout(layouts::solidMaterial()));
static_assert(validateLayout(layouts::skinned()));
static_assert(validateLayout(layouts::solidTextured()));
static_assert(validateLayout(layouts::lightmap()));

// 步长大小检查
static_assert(layouts::wire().stride() == 16);
static_assert(layouts::solid().stride() == 32);
static_assert(layouts::pick().stride() == 16);
static_assert(layouts::pbr().stride() == 48);
static_assert(layouts::solidMaterial().stride() == 32);
static_assert(layouts::skinned().stride() == 68);
static_assert(layouts::solidTextured().stride() == 40);
static_assert(layouts::lightmap().stride() == 56);

} // namespace mulan::Engine
