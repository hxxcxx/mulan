/**
 * @file Mesh.h
 * @brief CPU 侧网格数据，Engine 的通用几何表示
 * @author hxxcxx
 * @date 2026-04-21
 */

#pragma once

#include "../math/Math.h"
#include "../math/AABB.h"
#include "../render/RenderGeometry.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mulan::engine {

// ============================================================
// 网格 — CPU 侧拥有顶点与索引数据
//
// 交错顶点布局：position(3) + normal(3) + texCoord(2) = 8 floats
// 所有来源（文件导入、参数化生成、程序化创建）统一输出此类型。
// ============================================================

struct Mesh {
    std::vector<float>        vertices;       // 交错顶点数据
    std::vector<uint32_t>     indices;        // 三角形索引
    uint32_t                  vertexStride = sizeof(float) * 8;
    PrimitiveTopology         topology     = PrimitiveTopology::TriangleList;
    std::string               name;
    AABB                      bounds;

    // --- 便捷访问 ---

    uint32_t vertexCount() const {
        return vertexStride > 0
            ? static_cast<uint32_t>(vertices.size()) / (vertexStride / sizeof(float))
            : 0;
    }

    uint32_t indexCount() const {
        return static_cast<uint32_t>(indices.size());
    }

    uint32_t triangleCount() const {
        return static_cast<uint32_t>(indices.size()) / 3;
    }

    bool empty() const {
        return vertices.empty();
    }

    // --- 转换为渲染管线的零拷贝视图 ---

    RenderGeometry asRenderGeometry() const {
        RenderGeometry geo{};
        geo.vertexBytes  = std::as_bytes(std::span{vertices});
        geo.indexBytes   = std::as_bytes(std::span{indices});
        geo.vertexCount  = vertexCount();
        geo.indexCount   = indexCount();
        geo.vertexStride = vertexStride;
        geo.topology     = topology;
        return geo;
    }

    // --- 包围盒计算 ---

    void computeBounds() {
        bounds.reset();
        uint32_t floatsPerVert = vertexStride / sizeof(float);
        for (size_t i = 0; i + 2 < vertices.size(); i += floatsPerVert) {
            bounds.expand(Vec3{vertices[i], vertices[i + 1], vertices[i + 2]});
        }
    }
};

} // namespace mulan::engine
