/**
 * @file RenderSystem.h
 * @brief 渲染 System — 桥接 world::Entity → RenderCollector → engine::GpuResourceManager
 *
 * update() 内容：
 * 1. Destroyed → release GPU 资源
 * 2. Geometry/Material → 填 RenderCollector → 上传 GpuResourceManager
 * 3. 产出按 materialId 分组的 DrawBatch 列表 → ForwardPass 消费
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "System.h"
#include "../RenderCollector.h"
#include "mulan/engine/render/GpuResourceManager.h"
#include "mulan/engine/render/graph/ForwardPass.h"

#include <vector>

namespace mulan::world {

class RenderSystem : public System {
public:
    explicit RenderSystem(engine::GpuResourceManager& gpu);

    void update(World& world, float dt) override;

    RenderCollector& collector() { return m_collector; }
    const RenderCollector& collector() const { return m_collector; }

    /// RenderSystem 产出的按材质分组的绘制批次
    const std::vector<engine::DrawBatch>& drawBatches() const { return m_drawBatches; }

private:
    RenderCollector m_collector;
    engine::GpuResourceManager& m_gpu;
    std::vector<engine::DrawBatch> m_drawBatches;
};

} // namespace mulan::world
