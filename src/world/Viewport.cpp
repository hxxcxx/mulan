/**
 * @file Viewport.cpp
 * @brief Viewport 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "Viewport.h"
#include "World.h"
#include "mulan/engine/rhi/Device.h"
#include "mulan/engine/render/graph/ForwardPass.h"
#include "mulan/engine/render/graph/EdgePass.h"

namespace mulan::world {

Viewport::Viewport(World& world, engine::RHIDevice& device)
    : m_world(world)
    , m_device(device)
    , m_gpu(device)
    , m_renderSys(m_gpu)
    , m_camera(engine::CameraMode::Trackball) {
    m_camera.setOrthographic(true);
    m_camera.setOrthoSize(5.0);
    m_camera.setDistance(10.0);
}

bool Viewport::initRendering(int width, int height) {
    m_width  = width;
    m_height = height;
    m_camera.setViewport(width, height);

    auto fwd = std::make_unique<engine::ForwardPass>(
        m_device, m_gpu, m_camera, m_lightEnv);
    if (!fwd->init(engine::TextureFormat::RGBA8_UNorm,
                   engine::TextureFormat::D32_Float, true))
        return false;
    m_renderGraph.addPass(std::move(fwd));

    auto edge = std::make_unique<engine::EdgePass>(
        m_device, m_gpu, m_camera, m_lightEnv);
    if (!edge->init(engine::TextureFormat::RGBA8_UNorm,
                    engine::TextureFormat::D32_Float, true))
        return false;
    m_renderGraph.addPass(std::move(edge));

    m_renderingInited = true;
    return true;
}

void Viewport::render(float dt) {
    if (!m_renderingInited) return;

    // 1. World 逻辑
    if (!m_worldLogicUpdated) {
        m_world.updateLogic(dt);
        m_worldLogicUpdated = true;
    }

    // 2. RenderSystem：收集 + 上传 GPU + 产出 DrawBatch
    m_renderSys.update(m_world, dt);

    // 3. 把 DrawBatch 交给两个 Pass
    auto* fwd  = m_renderGraph.pass<engine::ForwardPass>(0);
    auto* edge = m_renderGraph.pass<engine::EdgePass>(1);
    if (fwd)  fwd->setDrawList(&m_renderSys.drawBatches());
    if (edge) edge->setDrawList(&m_renderSys.drawBatches());
}

void Viewport::renderPass(engine::CommandList* cmd) {
    if (!m_renderingInited || !cmd) return;

    engine::PassContext ctx;
    ctx.cmd    = cmd;
    ctx.width  = m_width;
    ctx.height = m_height;

    m_renderGraph.execute(ctx);
}

void Viewport::onFrameEnd() {
    m_world.clearDirty(EntityDirty::Created);
    m_worldLogicUpdated = false;
}

void Viewport::resize(int width, int height) {
    m_width  = width;
    m_height = height;
    m_camera.setViewport(width, height);
}

} // namespace mulan::world
