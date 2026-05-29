/**
 * @file RenderSystem.h
 * @brief 渲染 System — 桥接 world::Entity → RenderCollector → engine
 *
 * 职责：
 * 1. 遍历脏 Entity，按 GeometryData 类型分发到 RenderCollector
 * 2. 处理 Destroyed Entity 的资源释放
 * 3. 处理纯 Transform 变化（无需重新上传 mesh）
 * 4. flush 到 engine::GpuResourceManager（后期接入）
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "System.h"
#include "../RenderCollector.h"

namespace mulan::world {

class RenderSystem : public System {
public:
    RenderSystem();

    void update(World& world, float dt) override;

    /// 获取收集器（Viewport 或外部渲染后端从中取数据）
    RenderCollector& collector() { return m_collector; }
    const RenderCollector& collector() const { return m_collector; }

private:
    RenderCollector m_collector;
};

} // namespace mulan::world
