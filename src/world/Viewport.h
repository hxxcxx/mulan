/**
 * @file Viewport.h
 * @brief Viewport — 持有一个 GpuResourceManager + RenderSystem，驱动逐帧渲染
 *
 * 多 Viewport 场景下，World::updateLogic() 由第一个 render() 的 Viewport 执行，
 * 后续 Viewport 直接跳过。onFrameEnd() 统一收尾。
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "system/RenderSystem.h"
#include "mulan/engine/render/GpuResourceManager.h"

#include <cstdint>

namespace mulan::world {

class World;

class Viewport {
public:
    explicit Viewport(World& world);

    /// 逐帧渲染入口
    /// @param dt 帧间隔（秒）
    void render(float dt);

    /// 所有 Viewport 渲染完毕后的帧末清理
    void onFrameEnd();

    engine::GpuResourceManager& gpu() { return m_gpu; }
    const engine::GpuResourceManager& gpu() const { return m_gpu; }

    RenderSystem& renderSystem() { return m_renderSys; }
    const RenderSystem& renderSystem() const { return m_renderSys; }

private:
    World&                     m_world;
    engine::GpuResourceManager m_gpu;
    RenderSystem               m_renderSys;
    bool                       m_worldLogicUpdated = false;
};

} // namespace mulan::world
