/**
 * @file render_geometry.h
 * @brief GpuGeometry —— 几何资源的 GPU 缓冲区描述，由 AssetGpuRegistry 持有。
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include <mulan/math/math.h>
#include <mulan/graphics/vertex/vertex_layout.h>
#include "../rhi/pipeline_state.h"
#include "../rhi/device.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace mulan::engine {

using graphics::IndexType;
using graphics::VertexLayout;

// ============================================================
// GPU 端几何缓冲区 — 由 AssetGpuRegistry 按资产身份持有
//
// 上传后 vertexBuffer/indexBuffer 拥有 GPU 资源；资产从文档移除或
// 文档切换时，由 AssetGpuRegistry::clear() 释放（GpuGeometry 析构 →
// unique_ptr<Buffer> 释放 GPU 句柄）。
// ============================================================

struct GpuGeometry {
    std::unique_ptr<Buffer> vertexBuffer;
    std::unique_ptr<Buffer> indexBuffer;
    VertexLayout layout;  ///< 顶点布局（从 Mesh 带过来，供 draw 时用）
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    IndexType indexType = IndexType::UInt32;  ///< 索引宽度（从 Mesh 带过来，供 setIndexBuffer 用）
    bool uploaded = false;                    ///< 是否已上传到 GPU

    bool isValid() const { return uploaded && vertexBuffer; }
};

}  // namespace mulan::engine
