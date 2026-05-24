/**
 * @file CullVisitor.h
 * @brief 视锥裁剪访问器 — 遍历场景树，收集可见的 GeometryNode 到 RenderQueue
 * @author hxxcxx
 * @date 2026-04-23
 *
 * 职责：
 *  - 遍历 Scene 树（跳过不可见子树）
 *  - 对 GeometryNode 做视锥裁剪
 *  - 通过裁剪的节点生成 RenderItem 加入 RenderQueue
 *
 * 参考 other/ 的 CPMGsCullVisitor 设计。
 */
#pragma once

#include "SceneNode.h"
#include "Frustum.h"
#include "../Render/RenderGeometry.h"
#include "Camera/Camera.h"
#include "../RHI/Device.h"

#include <cstdint>

namespace MulanGeo::engine {

class CullVisitor {
public:
    CullVisitor(const Frustum& frustum, RenderQueue& queue, RHIDevice* device)
        : m_frustum(frustum), m_queue(queue), m_device(device) {}

    /// 访问一个节点（在 traverse 回调中使用）
    void visit(SceneNode& node) {
        if (!node.isEffectivelyVisible()) return;

        // 只处理有几何数据的节点
        if (!node.hasRenderData() && !node.hasEdgeData()) return;

        // 视锥裁剪
        const auto& bounds = node.worldBoundingBox();
        if (!bounds.isEmpty() && !m_frustum.intersects(bounds)) return;

        // 面几何 → RenderQueue
        if (node.hasRenderData()) {
            RenderItem item;
            item.geometry       = &node.cachedRenderGeometry();
            item.gpu            = node.ensureGpuGeometry(m_device);
            item.worldTransform = node.worldTransform();
            item.pickId         = node.pickId();
            item.materialIndex  = node.materialIndex();
            item.selected       = node.selected();

            m_queue.add(item);
        }

        // 边线几何 → RenderQueue（标记 isEdge）
        if (node.hasEdgeData()) {
            RenderItem edgeItem;
            edgeItem.geometry       = &node.cachedEdgeGeometry();
            edgeItem.gpu            = node.ensureGpuEdgeGeometry(m_device);
            edgeItem.worldTransform = node.worldTransform();
            edgeItem.pickId         = node.pickId();
            edgeItem.materialIndex  = node.materialIndex();
            edgeItem.selected       = node.selected();
            edgeItem.isEdge         = true;

            m_queue.add(edgeItem);
        }
    }

private:
    const Frustum& m_frustum;
    RenderQueue&   m_queue;
    RHIDevice*     m_device;
};

} // namespace MulanGeo::Engine
