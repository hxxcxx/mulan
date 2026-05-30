/**
 * @file RenderGeometry.h
 * @brief GPU无关的通用可绘制几何数据描述
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../math/Math.h"
#include "../rhi/VertexLayout.h"
#include "../rhi/PipelineState.h"
#include "../rhi/Device.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mulan::engine {

// ============================================================
// 可绘制几何数据 — 原始字节 + 元信息
// ============================================================

struct RenderGeometry {
    std::span<const std::byte> vertexBytes;    // 顶点数据（视图，不拥有）
    std::span<const std::byte> indexBytes;     // 索引数据（视图，不拥有，可为空）
    uint32_t           vertexCount  = 0;
    uint32_t           indexCount   = 0;
    uint32_t           vertexStride = 0;       // 单个顶点字节数
    VertexLayout       layout;                 // 怎么解释 vertexBytes
    PrimitiveTopology  topology = PrimitiveTopology::TriangleList;

    // 便捷：顶点数据总字节数
    uint32_t vertexDataSize() const { return vertexCount * vertexStride; }
    // 便捷：索引数据总字节数
    uint32_t indexDataSize() const {
        return static_cast<uint32_t>(indexBytes.size());
    }
    // 是否有索引
    bool hasIndex() const { return indexCount > 0 && !indexBytes.empty(); }
};

// ============================================================
// GPU 端几何缓冲区 — 由 GeometryNode 持有，节点销毁时自动释放
// ============================================================

struct GpuGeometry {
    ResourcePtr<Buffer> vertexBuffer;
    ResourcePtr<Buffer> indexBuffer;
    uint32_t vertexCount  = 0;
    uint32_t indexCount   = 0;
    uint32_t vertexStride = 0;
    bool     uploaded     = false;  ///< 是否已上传到 GPU

    bool isValid() const { return uploaded && vertexBuffer; }
};

// ============================================================
// 绘制项 — 一次 draw call 的全部数据
// ============================================================

struct RenderItem {
    const RenderGeometry* geometry       = nullptr;
    GpuGeometry*          gpu            = nullptr;   ///< 由 GeometryNode 持有
    Mat4                  worldTransform = Mat4(1.0);
    uint32_t              pickId         = 0;
    uint16_t              materialIndex  = 0xFFFF;  ///< 材质索引 (0xFFFF = 默认)
    uint8_t               renderPass     = 0;       ///< 0=Opaque, 1=Transparent
    bool                  selected       = false;   ///< 面/节点是否被选中
    bool                  isEdge         = false;   ///< true=边线渲染项

    /// 排序键：低32位材质索引（不透明：前排距离近优先；透明：后排距离远优先）
    uint64_t              sortKey        = 0;

    /// 计算不透明排序键（相同材质分组以减少状态切换，材质内按距离从近到远）
    void computeOpaqueSortKey(const Vec3& cameraPos) {
        double distSq = glm::length2(Vec3(worldTransform * Vec4(0,0,0,1)) - cameraPos);
        uint32_t distBits = static_cast<uint32_t>(distSq);       // 粗略距离桶
        sortKey = (static_cast<uint64_t>(materialIndex) << 32) | distBits;
    }

    /// 计算透明排序键（从远到近排序，保证正确的半透明混合）
    void computeTransparentSortKey(const Vec3& cameraPos) {
        double distSq = glm::length2(Vec3(worldTransform * Vec4(0,0,0,1)) - cameraPos);
        uint32_t distBits = static_cast<uint32_t>(distSq);
        sortKey = (static_cast<uint64_t>(0xFFFF - materialIndex) << 32)
                | (0xFFFFFFFFu - distBits);  // 翻转使远距优先
    }
};

// ============================================================
// 渲染队列 — 由上层填充，Renderer 消费
//
// 支持不透明/半透明两桶分离排序：
//   - 不透明：按材质分组以减少状态切换
//   - 半透明：从远到近以保证混合正确性
// ============================================================

class RenderQueue {
public:
    void add(const RenderItem& item) { m_items.push_back(item); }
    void clear() { m_items.clear(); m_opaqueSplit = 0; }
    void reserve(size_t n) { m_items.reserve(n); }

    size_t size() const { return m_items.size(); }
    bool   empty() const { return m_items.empty(); }

    // 遍历
    const RenderItem* data() const { return m_items.data(); }
    std::span<const RenderItem> items() const { return m_items; }

    const RenderItem& operator[](size_t i) const { return m_items[i]; }

    // --- 排序 ---

    /// 分桶 + 排序（在每帧 collect 完成后、渲染前调用）
    void sort(const Vec3& cameraPos) {
        // 1. 分区：不透明在前，边线中间，半透明在后
        auto mid1 = std::stable_partition(m_items.begin(), m_items.end(),
            [](const RenderItem& a) { return a.renderPass == 0 && !a.isEdge; });
        auto mid2 = std::stable_partition(mid1, m_items.end(),
            [](const RenderItem& a) { return a.isEdge; });
        m_opaqueSplit = static_cast<size_t>(mid1 - m_items.begin());
        m_edgeSplit = static_cast<size_t>(mid2 - m_items.begin());

        // 2. 不透明区域：按材质分组排序
        for (size_t i = 0; i < m_opaqueSplit; ++i)
            m_items[i].computeOpaqueSortKey(cameraPos);
        std::sort(m_items.begin(), mid1,
            [](const RenderItem& a, const RenderItem& b) { return a.sortKey < b.sortKey; });

        // 3. 边线区域：不需要排序

        // 4. 半透明区域：按距离从远到近
        for (size_t i = m_edgeSplit; i < m_items.size(); ++i)
            m_items[i].computeTransparentSortKey(cameraPos);
        std::sort(m_items.begin() + m_edgeSplit, m_items.end(),
            [](const RenderItem& a, const RenderItem& b) { return a.sortKey < b.sortKey; });
    }

    /// 不透明子范围
    std::span<const RenderItem> opaqueItems() const {
        return { m_items.data(), m_opaqueSplit };
    }

    /// 边线子范围
    std::span<const RenderItem> edgeItems() const {
        return { m_items.data() + m_opaqueSplit, m_edgeSplit - m_opaqueSplit };
    }

    /// 半透明子范围
    std::span<const RenderItem> transparentItems() const {
        return { m_items.data() + m_edgeSplit, m_items.size() - m_edgeSplit };
    }

private:
    std::vector<RenderItem> m_items;
    size_t m_opaqueSplit = 0;  ///< 不透明/边线分割点
    size_t m_edgeSplit   = 0;  ///< 边线/半透明分割点
};

} // namespace mulan::engine
