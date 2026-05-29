/**
 * @file Viewport.cpp
 * @brief Viewport 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "Viewport.h"
#include "World.h"

namespace mulan::world {

Viewport::Viewport(World& world)
    : m_world(world)
    , m_gpu()
    , m_renderSys(m_gpu) {
}

void Viewport::render(float dt) {
    // Phase 1: World 逻辑更新（多个 Viewport 只跑一次）
    if (!m_worldLogicUpdated) {
        m_world.updateLogic(dt);
        m_worldLogicUpdated = true;
    }

    // Phase 2: 渲染收集 + GPU 上传
    m_renderSys.update(m_world, dt);

    // Phase 3..N: 后期接入 RenderGraph / ID Buffer / ...
}

void Viewport::onFrameEnd() {
    m_world.clearDirty(EntityDirty::Created);
    m_worldLogicUpdated = false;
}

} // namespace mulan::world
