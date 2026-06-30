/**
 * @file Viewport.cpp
 * @brief Viewport 实现 — 吸收 EngineView 的完整视图层
 * @author hxxcxx
 * @date 2026-05-29 (原始) / 2026-06-01 (重构)
 */

#include "Viewport.h"
#include "World.h"
#include "system/RenderSystem.h"
#include "mulan/engine/rhi/Device.h"
#include "mulan/engine/rhi/RenderTypes.h"
#include "mulan/engine/render/graph/ForwardPass.h"
#include "mulan/engine/render/graph/EdgePass.h"

#include <cstdio>
#include <cstring>

namespace mulan::world {

// ============================================================
// 旧构造（向后兼容）
// ============================================================

Viewport::Viewport(World& world, engine::RHIDevice& device)
    : m_device(&device)
    , m_world(&world)
    , m_gpuStorage(std::make_unique<engine::GpuResourceManager>(device))
    , m_renderSysStorage(std::make_unique<RenderSystem>(*m_gpuStorage, m_camera))
    , m_defaultOp(std::make_unique<engine::CameraManipulator>())
{
    m_gpu      = m_gpuStorage.get();
    m_renderSys = m_renderSysStorage.get();
    m_defaultOp->setState(engine::Operator::State::Active);
    m_defaultOp->onActivate(m_camera);
    m_camera.setOrthographic(true);
    m_camera.setOrthoSize(5.0);
    m_camera.setDistance(10.0);
}

// ============================================================
// 新构造（延迟初始化）
// ============================================================

Viewport::Viewport()
    : m_defaultOp(std::make_unique<engine::CameraManipulator>())
{
    m_defaultOp->setState(engine::Operator::State::Active);
    m_defaultOp->onActivate(m_camera);
    m_camera.setOrthographic(true);
    m_camera.setOrthoSize(5.0);
    m_camera.setDistance(10.0);
}

Viewport::~Viewport() {
    shutdown();
}

// ============================================================
// init（窗口模式）
// ============================================================

bool Viewport::init(const ViewConfig& cfg, int width, int height) {
    if (m_initialized) return true;

    m_width  = width;
    m_height = height;

    // --- 从 ViewConfig 构造 DeviceCreateInfo ---
    engine::NativeWindowHandle window = cfg.toNativeWindowHandle();
    if (!window.valid()) return false;

    engine::RenderConfig renderCfg = cfg.toRenderConfig();

    engine::DeviceCreateInfo ci;
    ci.backend          = cfg.backend;
    ci.window           = window;
    ci.renderConfig     = renderCfg;
    ci.enableValidation = cfg.enableValidation;

    m_ownedDevice = engine::RHIDevice::create(ci);
    if (!m_ownedDevice) return false;
    m_device = m_ownedDevice.get();

    // --- SwapChain ---
    engine::SwapChainDesc scDesc;
    scDesc.width       = static_cast<uint32_t>(width);
    scDesc.height      = static_cast<uint32_t>(height);
    scDesc.format      = engine::TextureFormat::BGRA8_UNorm;
    scDesc.bufferCount = cfg.bufferCount;
    scDesc.sampleCount = renderCfg.sampleCount();
    scDesc.vsync       = cfg.vsync;
    std::memcpy(scDesc.clearColor, renderCfg.clearColor, sizeof(scDesc.clearColor));
    scDesc.clearDepth  = renderCfg.clearDepth;

    m_swapchain = m_device->createSwapChain(scDesc);
    if (!m_swapchain) { cleanup(); return false; }

    // --- GPU / RenderSystem ---
    m_gpuStorage  = std::make_unique<engine::GpuResourceManager>(*m_device);
    m_renderSysStorage = std::make_unique<RenderSystem>(*m_gpuStorage, m_camera);
    m_gpu      = m_gpuStorage.get();
    m_renderSys = m_renderSysStorage.get();

    // --- RenderGraph ---
    if (!initRendering(width, height)) { cleanup(); return false; }

    // --- Camera ---
    m_camera.setViewport(width, height);
    m_camera.fitToBox(engine::AABB(engine::Vec3(-1, -1, -1), engine::Vec3(1, 1, 1)));

    m_initialized = true;
    return true;
}

// ============================================================
// initOffscreen（离屏模式）
// ============================================================

bool Viewport::initOffscreen(int width, int height) {
    if (m_initialized) return true;

    m_width  = width;
    m_height = height;

    engine::RenderConfig config;
    config.bufferCount   = 2;
    config.vsync         = false;
    config.depthBuffer   = true;
    config.stencilBuffer = false;

    engine::DeviceCreateInfo ci;
    ci.backend          = engine::GraphicsBackend::Vulkan;
    ci.window           = {};
    ci.renderConfig     = config;
    ci.enableValidation = true;

    m_ownedDevice = engine::RHIDevice::create(ci);
    if (!m_ownedDevice) return false;
    m_device = m_ownedDevice.get();

    // --- RenderTarget ---
    engine::RenderTargetDesc rtDesc;
    rtDesc.width       = static_cast<uint32_t>(width);
    rtDesc.height      = static_cast<uint32_t>(height);
    rtDesc.colorFormat = engine::TextureFormat::RGBA8_UNorm;
    rtDesc.depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;
    rtDesc.hasDepth    = true;

    m_renderTarget = m_device->createRenderTarget(rtDesc);
    if (!m_renderTarget) { cleanup(); return false; }

    // --- Staging buffer ---
    uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
    m_stagingBuffer = m_device->createBuffer(
        engine::BufferDesc::staging(pixelBytes, "ReadbackStaging"));

    // --- GPU / RenderSystem ---
    m_gpuStorage  = std::make_unique<engine::GpuResourceManager>(*m_device);
    m_renderSysStorage = std::make_unique<RenderSystem>(*m_gpuStorage, m_camera);
    m_gpu      = m_gpuStorage.get();
    m_renderSys = m_renderSysStorage.get();

    // --- RenderGraph ---
    if (!initRendering(width, height)) { cleanup(); return false; }

    // --- Camera ---
    m_camera.setViewport(width, height);
    m_camera.fitToBox(engine::AABB(engine::Vec3(-1, -1, -1), engine::Vec3(1, 1, 1)));

    m_initialized = true;
    return true;
}

// ============================================================
// shutdown
// ============================================================

void Viewport::shutdown() {
    if (!m_initialized && !m_ownedDevice) return;
    if (m_device) m_device->waitIdle();

    m_viewCubeRenderer.reset();
    m_renderSysStorage.reset();
    m_gpuStorage.reset();
    m_stagingBuffer.reset();
    m_renderTarget.reset();
    m_swapchain.reset();

    m_gpu       = nullptr;
    m_renderSys = nullptr;
    m_device    = nullptr;
    m_ownedDevice.reset();

    m_initialized    = false;
    m_renderingInited = false;
}

// ============================================================
// World 绑定
// ============================================================

void Viewport::setWorld(World* world) {
    m_world = world;
}

// ============================================================
// initRendering（旧路径 + 新路径共用）
// ============================================================

bool Viewport::initRendering(int width, int height) {
    m_width  = width;
    m_height = height;
    m_camera.setViewport(width, height);

    // 用实际渲染目标的格式创建 Pass，避免 PSO 的 RenderPass 与 SwapChain/RT 不兼容
    engine::TextureFormat colorFmt = m_renderTarget
        ? m_renderTarget->colorFormat()
        : (m_swapchain ? m_swapchain->colorFormat() : engine::TextureFormat::RGBA8_UNorm);
    engine::TextureFormat depthFmt = m_renderTarget
        ? m_renderTarget->depthFormat()
        : (m_swapchain ? m_swapchain->depthFormat() : engine::TextureFormat::D32_Float);

    auto fwd = std::make_unique<engine::ForwardPass>(
        *m_device, *m_gpu, m_camera, m_lightEnv);
    if (!fwd->init(colorFmt, depthFmt, true))
        return false;
    m_renderGraph.addPass(std::move(fwd));

    auto edge = std::make_unique<engine::EdgePass>(
        *m_device, *m_gpu, m_camera, m_lightEnv);
    if (!edge->init(colorFmt, depthFmt, true))
        return false;
    m_renderGraph.addPass(std::move(edge));

    // ViewCube
    if (!initSceneRenderer()) {
        std::fprintf(stderr, "[Viewport] ViewCube init failed (non-fatal)\n");
    }

    m_renderingInited = true;
    return true;
}

bool Viewport::initSceneRenderer() {
    engine::TextureFormat colorFmt = m_renderTarget
        ? m_renderTarget->colorFormat()
        : (m_swapchain ? m_swapchain->colorFormat() : engine::TextureFormat::RGBA8_UNorm);
    engine::TextureFormat depthFmt = m_renderTarget
        ? m_renderTarget->depthFormat()
        : (m_swapchain ? m_swapchain->depthFormat() : engine::TextureFormat::D32_Float);

    m_viewCubeRenderer = std::make_unique<engine::ViewCubeRenderer>(m_device);
    if (!m_viewCubeRenderer->init(colorFmt, depthFmt)) {
        m_viewCubeRenderer.reset();
        return false;
    }
    return true;
}

void Viewport::cleanup() {
    m_viewCubeRenderer.reset();
    m_renderGraph = engine::RenderGraph();
    m_stagingBuffer.reset();
    m_renderTarget.reset();
    m_swapchain.reset();
    m_renderSysStorage.reset();
    m_gpuStorage.reset();
    m_gpu       = nullptr;
    m_renderSys = nullptr;
}

// ============================================================
// renderFrame（新路径：完整帧循环）
// ============================================================

void Viewport::renderFrame() {
    if (!m_initialized || !m_world) return;

    // 1. World 逻辑
    if (!m_worldLogicUpdated) {
        m_world->updateLogic(0);
        m_worldLogicUpdated = true;
    }

    // 2. RenderSystem 收集
    // 先确保 RenderSystem 拿到 Pass 的 PSO（必须在 update 之前，否则 rebuild
    // 用 nullptr PSO 构建 draw command，导致全部 draw 被守卫跳过）
    auto* fwd  = m_renderGraph.pass<engine::ForwardPass>(0);
    auto* edge = m_renderGraph.pass<engine::EdgePass>(1);
    if (fwd && !m_renderSys->hasFacePso())
        m_renderSys->setFacePso(fwd->pipelineState());
    if (edge && !m_renderSys->hasEdgePso())
        m_renderSys->setEdgePso(edge->pipelineState());

    m_renderSys->update(*m_world, 0);

    // 3. 传递 MeshDrawCommand 给 Pass（Phase 3 路径）
    if (fwd) {
        fwd->setDrawCommands(m_renderSys->staticFaceCommands());
    }
    if (edge) {
        edge->setDrawCommands(m_renderSys->staticEdgeCommands());
    }

    // 4. GPU 提交
    m_device->beginFrame();
    auto* cmd = m_device->frameCommandList();
    cmd->begin();

    if (m_renderTarget)
        cmd->beginRenderPass(m_renderTarget->renderPassBeginInfo());
    else if (m_swapchain)
        cmd->beginRenderPass(m_swapchain->renderPassBeginInfo());

    engine::Viewport vp;
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(m_width);
    vp.height   = static_cast<float>(m_height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->setViewport(vp);

    engine::ScissorRect sc;
    sc.x      = 0;
    sc.y      = 0;
    sc.width  = static_cast<int32_t>(m_width);
    sc.height = static_cast<int32_t>(m_height);
    cmd->setScissorRect(sc);

    engine::PassContext ctx;
    ctx.cmd    = cmd;
    ctx.width  = m_width;
    ctx.height = m_height;
    m_renderGraph.execute(ctx);

    // ViewCube（叠加在右下角）
    if (m_viewCubeRenderer) {
        m_viewCubeRenderer->render(cmd, m_camera,
                                   static_cast<uint32_t>(m_width),
                                   static_cast<uint32_t>(m_height));
    }

    cmd->endRenderPass();
    cmd->end();

    if (m_renderTarget)
        m_device->submitOffscreen();
    else
        m_device->submitAndPresent(m_swapchain.get());

    // 5. 清脏
    onFrameEnd();
}

// ============================================================
// render / renderPass（旧路径）
// ============================================================

void Viewport::render(float dt) {
    if (!m_renderingInited || !m_world) return;

    // 1. World 逻辑
    if (!m_worldLogicUpdated) {
        m_world->updateLogic(dt);
        m_worldLogicUpdated = true;
    }

    // 2. RenderSystem：收集 + 上传 GPU + 产出 MeshDrawCommand
    m_renderSys->update(*m_world, dt);

    // 3. 把 MeshDrawCommand 交给两个 Pass
    auto* fwd  = m_renderGraph.pass<engine::ForwardPass>(0);
    auto* edge = m_renderGraph.pass<engine::EdgePass>(1);
    if (fwd) {
        if (!m_renderSys->hasFacePso())
            m_renderSys->setFacePso(fwd->pipelineState());
        fwd->setDrawCommands(m_renderSys->staticFaceCommands());
    }
    if (edge) {
        if (!m_renderSys->hasEdgePso())
            m_renderSys->setEdgePso(edge->pipelineState());
        edge->setDrawCommands(m_renderSys->staticEdgeCommands());
    }
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
    if (m_world) {
        // 清除 RenderSystem 已消费的所有脏标记
        using ED = EntityDirty;
        m_world->clearDirty(ED::Created | ED::Destroyed | ED::Transform
                          | ED::Geometry | ED::Visibility | ED::Material
                          | ED::Selection | ED::Parent);
    }
    m_worldLogicUpdated = false;
}

void Viewport::resize(int width, int height) {
    m_width  = width;
    m_height = height;

    if (m_device && m_initialized) {
        m_device->waitIdle();

        if (m_renderTarget) {
            m_renderTarget->resize(width, height);

            m_stagingBuffer.reset();
            uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
            m_stagingBuffer = m_device->createBuffer(
                engine::BufferDesc::staging(pixelBytes, "ReadbackStaging"));
        } else if (m_swapchain) {
            m_swapchain->resize(width, height);
        }
    }
    m_camera.setViewport(width, height);
}

// ============================================================
// 输入
// ============================================================

void Viewport::handleInput(const engine::InputEvent& event) {
    engine::Operator* op = activeOperator();
    if (!op) return;

    op->handleEvent(event, m_camera);

    // 状态机检测：模态 Operator 调用 finish() 后，由 Viewport 在外部触发
    // 回调并 pop（避免在 Operator 成员函数内析构自身的 UB）。
    if (op->isFinished() && !m_opStack.empty()) {
        auto finishedHook = op->finishHook();  // 拷贝回调（pop 会析构 Operator）
        if (finishedHook) finishedHook(*op);
        popOperator();
    }
}

// ============================================================
// Operator（LIFO 栈）
// ============================================================

void Viewport::pushOperator(std::unique_ptr<engine::Operator> op) {
    if (!op) return;

    // 挂起当前栈顶（若有）
    if (auto* cur = activeOperator()) {
        cur->setState(engine::Operator::State::Inactive);
        cur->onDeactivate(m_camera);
    }

    op->setState(engine::Operator::State::Active);
    op->onActivate(m_camera);
    m_opStack.push_back(std::move(op));
}

void Viewport::popOperator() {
    if (m_opStack.empty()) return;

    // 析构栈顶（先 deactivate）
    auto top = std::move(m_opStack.back());
    m_opStack.pop_back();
    top->setState(engine::Operator::State::Inactive);
    top->onDeactivate(m_camera);
    top.reset();  // 析构

    // 恢复下层
    if (auto* next = activeOperator()) {
        next->setState(engine::Operator::State::Active);
        next->onActivate(m_camera);
    }
}

engine::Operator* Viewport::activeOperator() const {
    if (!m_opStack.empty()) return m_opStack.back().get();
    return m_defaultOp.get();
}

// ============================================================
// 离屏回读
// ============================================================

bool Viewport::readbackPixels(std::vector<uint8_t>& pixels) {
    if (!m_renderTarget || !m_stagingBuffer || !m_device) return false;

    m_device->waitIdle();

    auto cmd = m_device->createCommandList();
    cmd->begin();
    cmd->transitionResource(m_renderTarget->colorTexture(), engine::ResourceState::CopySrc);
    cmd->copyTextureToBuffer(m_renderTarget->colorTexture(), m_stagingBuffer.get());
    cmd->end();

    m_device->executeCommandList(cmd.get());
    m_device->waitIdle();

    uint32_t byteSize = static_cast<uint32_t>(m_width) * m_height * 4;
    pixels.resize(byteSize);
    return m_stagingBuffer->readback(0, byteSize, pixels.data());
}

} // namespace mulan::world
