/**
 * @file render_geometry.h
 * @brief GPU无关的通用可绘制几何数据描述
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../math/math.h"
#include "../vertex/vertex_layout.h"
#include "../rhi/pipeline_state.h"
#include "../rhi/device.h"

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
    std::unique_ptr<Buffer> vertexBuffer;
    std::unique_ptr<Buffer> indexBuffer;
    VertexLayout            layout;       ///< 顶点布局（从 Mesh 带过来，供 draw 时用）
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
        double distSq = (Vec3(worldTransform * Vec4(0,0,0,1)) - cameraPos).lengthSq();
        uint32_t distBits = static_cast<uint32_t>(distSq);       // 粗略距离桶
        sortKey = (static_cast<uint64_t>(materialIndex) << 32) | distBits;
    }

    /// 计算透明排序键（从远到近排序，保证正确的半透明混合）
    void computeTransparentSortKey(const Vec3& cameraPos) {
        double distSq = (Vec3(worldTransform * Vec4(0,0,0,1)) - cameraPos).lengthSq();
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
    void add(const RenderItem& item) { items_.push_back(item); }
    void clear() { items_.clear(); opaque_split_ = 0; }
    void reserve(size_t n) { items_.reserve(n); }

    size_t size() const { return items_.size(); }
    bool   empty() const { return items_.empty(); }

    // 遍历
    const RenderItem* data() const { return items_.data(); }
    std::span<const RenderItem> items() const { return items_; }

    const RenderItem& operator[](size_t i) const { return items_[i]; }

    // --- 排序 ---

    /// 分桶 + 排序（在每帧 collect 完成后、渲染前调用）
    void sort(const Vec3& cameraPos) {
        // 1. 分区：不透明在前，边线中间，半透明在后
        auto mid1 = std::stable_partition(items_.begin(), items_.end(),
            [](const RenderItem& a) { return a.renderPass == 0 && !a.isEdge; });
        auto mid2 = std::stable_partition(mid1, items_.end(),
            [](const RenderItem& a) { return a.isEdge; });
        opaque_split_ = static_cast<size_t>(mid1 - items_.begin());
        edge_split_ = static_cast<size_t>(mid2 - items_.begin());

        // 2. 不透明区域：按材质分组排序
        for (size_t i = 0; i < opaque_split_; ++i)
            items_[i].computeOpaqueSortKey(cameraPos);
        std::sort(items_.begin(), mid1,
            [](const RenderItem& a, const RenderItem& b) { return a.sortKey < b.sortKey; });

        // 3. 边线区域：不需要排序

        // 4. 半透明区域：按距离从远到近
        for (size_t i = edge_split_; i < items_.size(); ++i)
            items_[i].computeTransparentSortKey(cameraPos);
        std::sort(items_.begin() + edge_split_, items_.end(),
            [](const RenderItem& a, const RenderItem& b) { return a.sortKey < b.sortKey; });
    }

    /// 不透明子范围
    std::span<const RenderItem> opaqueItems() const {
        return { items_.data(), opaque_split_ };
    }

    /// 边线子范围
    std::span<const RenderItem> edgeItems() const {
        return { items_.data() + opaque_split_, edge_split_ - opaque_split_ };
    }

    /// 半透明子范围
    std::span<const RenderItem> transparentItems() const {
        return { items_.data() + edge_split_, items_.size() - edge_split_ };
    }

private:
    std::vector<RenderItem> items_;
    size_t opaque_split_ = 0;  ///< 不透明/边线分割点
    size_t edge_split_   = 0;  ///< 边线/半透明分割点
};

} // namespace mulan::engine
