/**
 * @file mesh.h
 * @brief CPU 侧网格数据 —— 顶点+索引缓冲，按 VertexLayout 解释
 * @author hxxcxx
 * @date 2026-04-21
 */

#pragma once

#include <mulan/math/math.h>
#include <mulan/math/math.h>
#include "primitive_types.h"
#include "vertex/vertex_layout.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace mulan::graphics {

struct RenderGeometryView {
    VertexLayout              layout;
    std::span<const std::byte> vertexBytes;
    std::span<const std::byte> indexBytes;
    uint32_t                  vertexCount  = 0;
    uint32_t                  indexCount   = 0;
    uint32_t                  vertexStride = 0;
    PrimitiveTopology         topology     = PrimitiveTopology::TriangleList;
};

// ============================================================
// 网格 —— CPU 侧拥有的顶点与索引裸字节缓冲
//
// 顶点布局的唯一来源是 layout 字段：vertices 按裸字节存储，
// 其含义由 layout 完整描述（semantic / format / offset / stride）。
// 所有来源（文件导入、参数化生成、程序化创建）统一输出此类型，
// 构造时必须给出 layout。
// ============================================================

struct Mesh {
    VertexLayout              layout;     // 唯一布局来源
    std::vector<std::byte>    vertices;   // 裸字节，按 layout.stride() 切分
    std::vector<std::byte>    indices;    // 裸字节，宽度由 indexType 决定
    IndexType                 indexType  = IndexType::UInt32;
    PrimitiveTopology         topology   = PrimitiveTopology::TriangleList;
    math::AABB3                      bounds;

    // --- 便捷访问 ---

    uint32_t vertexStride() const { return layout.stride(); }

    uint32_t vertexCount() const {
        return layout.stride() > 0
            ? static_cast<uint32_t>(vertices.size()) / layout.stride()
            : 0;
    }

    uint32_t indexCount() const {
        uint8_t isz = indexTypeSize(indexType);
        return isz > 0 ? static_cast<uint32_t>(indices.size()) / isz : 0;
    }

    uint32_t triangleCount() const {
        return static_cast<uint32_t>(indexCount()) / 3;
    }

    bool empty() const {
        return vertices.empty();
    }

    // --- 转换为渲染管线的零拷贝视图 ---

    RenderGeometryView asRenderGeometry() const {
        RenderGeometryView geo{};
        geo.layout        = layout;
        geo.vertexBytes   = std::span<const std::byte>{vertices};
        geo.indexBytes    = std::span<const std::byte>{indices};
        geo.vertexCount   = vertexCount();
        geo.indexCount    = indexCount();
        geo.vertexStride  = layout.stride();
        geo.topology      = topology;
        return geo;
    }

    // --- 包围盒计算（按 layout 中 Position 的实际偏移读取）---

    void computeBounds() {
        bounds.reset();
        const uint16_t off = layout.offsetOf(VertexSemantic::Position);
        if (off == 0xFFFF) return;  // 布局无 Position，无法计算
        const uint16_t stride = layout.stride();
        if (stride == 0) return;
        const auto* position = layout.find(VertexSemantic::Position);
        if (!position) return;

        for (size_t p = off; p + position->size() <= vertices.size(); p += stride) {
            math::Vec3 v{};
            if (position->format == VertexFormat::Float3) {
                float f[3]{};
                std::memcpy(f, &vertices[p], sizeof(f));
                v = math::Vec3(f[0], f[1], f[2]);
            } else if (position->format == VertexFormat::Float4) {
                float f[4]{};
                std::memcpy(f, &vertices[p], sizeof(f));
                v = math::Vec3(f[0], f[1], f[2]);
            } else {
                continue;
            }
            bounds.expand(math::Point3(v));
        }
    }
};

} // namespace mulan::graphics
