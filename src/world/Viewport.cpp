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
    m_renderingInited = true;
    return true;
}

void Viewport::render(float dt) {
    if (!m_renderingInited) return;

    // Phase 1: World 逻辑更新
    if (!m_worldLogicUpdated) {
        m_world.updateLogic(dt);
        m_worldLogicUpdated = true;
    }

    // Phase 2: 渲染收集 + GPU 上传
    m_renderSys.update(m_world, dt);

    // Phase 3: 填充 draw items
    auto* fwd = m_renderGraph.pass<engine::ForwardPass>(0);
    if (fwd) {
        fwd->items().clear();
        m_world.forEachEntity([&](Entity* e) {
            if (!e->geometry() || !e->visible()) return;
            engine::ForwardPass::DrawItem item;
            item.key            = e->id();
            item.worldTransform = e->worldTransform();
            item.color          = e->color();
            fwd->items().push_back(item);
        });
    }

    // Phase 4: RenderGraph 执行（后期通过外部传入 CommandList）
    // PassContext ctx{cmd, m_width, m_height};
    // m_renderGraph.execute(ctx);
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
