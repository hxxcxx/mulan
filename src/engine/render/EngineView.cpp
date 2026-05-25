/**
 * @file EngineView.cpp
 * @brief EngineView 实现 — 帧循环调度 + 设备生命周期
 * @author hxxcxx
 * @date 2026-04-17
 */

#include "EngineView.h"
#include "../scene/SceneNode.h"

#include <cstdio>
#include <cstring>

namespace mulan::engine {

// ============================================================
// 构造 / 析构
// ============================================================

EngineView::EngineView() {
    m_operator = std::make_unique<CameraManipulator>();
}

EngineView::~EngineView() {
    shutdown();
}

// ============================================================
// 初始化
// ============================================================

bool EngineView::init(const ViewConfig& cfg, int width, int height) {
    if (m_initialized) return true;

    m_width  = width;
    m_height = height;

    // --- 从 ViewConfig 构造 NativeWindowHandle & DeviceCreateInfo ---
    NativeWindowHandle window = cfg.toNativeWindowHandle();
    if (!window.valid()) return false;

    RenderConfig renderCfg = cfg.toRenderConfig();

    DeviceCreateInfo ci;
    ci.backend          = cfg.backend;
    ci.window           = window;
    ci.renderConfig     = renderCfg;
    ci.enableValidation = cfg.enableValidation;

    m_device = RHIDevice::create(ci);
    if (!m_device) return false;
    auto* dev = m_device.get();

    // --- SwapChain ---
    SwapChainDesc scDesc;
    scDesc.width       = static_cast<uint32_t>(width);
    scDesc.height      = static_cast<uint32_t>(height);
    scDesc.format      = TextureFormat::BGRA8_UNorm;
    scDesc.bufferCount = cfg.bufferCount;
    scDesc.sampleCount = renderCfg.sampleCount();
    scDesc.vsync       = cfg.vsync;
    std::memcpy(scDesc.clearColor, renderCfg.clearColor, sizeof(scDesc.clearColor));
    scDesc.clearDepth  = renderCfg.clearDepth;

    m_swapchain = dev->createSwapChain(scDesc);
    if (!m_swapchain) { cleanup(); return false; }

    // --- Scene Renderer（含 Shader/PSO/UBO）---
    if (!initSceneRenderer()) { cleanup(); return false; }

    // --- Camera ---
    m_camera.setViewport(width, height);
    m_camera.fitToBox(AABB(Vec3(-1, -1, -1), Vec3(1, 1, 1)));

    m_initialized = true;
    return true;
}

bool EngineView::initOffscreen(int width, int height) {
    if (m_initialized) return true;

    m_width  = width;
    m_height = height;

    RenderConfig config;
    config.bufferCount   = 2;
    config.vsync         = false;
    config.depthBuffer   = true;
    config.stencilBuffer = false;

    DeviceCreateInfo ci;
    ci.backend          = GraphicsBackend::Vulkan;
    ci.window           = {};
    ci.renderConfig     = config;
    ci.enableValidation = true;

    m_device = RHIDevice::create(ci);
    if (!m_device) return false;
    auto* dev = m_device.get();

    // --- RenderTarget ---
    RenderTargetDesc rtDesc;
    rtDesc.width       = static_cast<uint32_t>(width);
    rtDesc.height      = static_cast<uint32_t>(height);
    rtDesc.colorFormat = TextureFormat::RGBA8_UNorm;
    rtDesc.depthFormat = TextureFormat::D24_UNorm_S8_UInt;
    rtDesc.hasDepth    = true;

    m_renderTarget = dev->createRenderTarget(rtDesc);
    if (!m_renderTarget) { cleanup(); return false; }

    // --- Staging buffer ---
    uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
    m_stagingBuffer = dev->createBuffer(
        BufferDesc::staging(pixelBytes, "ReadbackStaging"));

    // --- Scene Renderer ---
    if (!initSceneRenderer()) { cleanup(); return false; }

    // --- Camera ---
    m_camera.setViewport(width, height);
    m_camera.fitToBox(AABB(Vec3(-1, -1, -1), Vec3(1, 1, 1)));

    m_initialized = true;
    return true;
}

// ============================================================
// Resize
// ============================================================

void EngineView::resize(int width, int height) {
    if (!m_initialized) return;
    m_width  = width;
    m_height = height;

    m_device->waitIdle();

    auto* dev = m_device.get();
    if (m_renderTarget) {
        m_renderTarget->resize(width, height);

        m_stagingBuffer.reset();
        uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
        m_stagingBuffer = dev->createBuffer(
            BufferDesc::staging(pixelBytes, "ReadbackStaging"));
    } else {
        m_swapchain->resize(width, height);
    }
    m_camera.setViewport(width, height);
}

// ============================================================
// 帧循环
// ============================================================

void EngineView::renderFrame() {
    if (!m_initialized) return;

    // 收集可见节点：增量更新世界变换 → 视锥裁剪
    m_renderQueue.clear();
    if (m_scene) {
        m_scene->updateWorldTransforms();
        auto frustum = m_camera.frustum();
        CullVisitor cull(frustum, m_renderQueue, m_device.get());
        m_scene->traverseVisible([&](SceneNode& node) {
            cull.visit(node);
        });
    }

    // 排序：不透明按材质分组，半透明从远到近
    m_renderQueue.sort(m_camera.eyePosition());

    m_device->beginFrame();
    auto* cmd = m_device->frameCommandList();

    cmd->begin();

    // --- begin render pass ---
    if (m_renderTarget) {
        cmd->beginRenderPass(m_renderTarget->renderPassBeginInfo());
    } else {
        cmd->beginRenderPass(m_swapchain->renderPassBeginInfo());
    }

    ScissorRect sc;
    sc.x      = 0;
    sc.y      = 0;
    sc.width  = static_cast<int32_t>(m_width);
    sc.height = static_cast<int32_t>(m_height);
    cmd->setScissorRect(sc);

    // 委托给 SceneRenderer（含 CameraUBO 更新 + PSO 设置 + 绘制）
    m_sceneRenderer->render(m_renderQueue, m_camera, cmd, m_lightEnv);

    // --- end render pass ---
    cmd->endRenderPass();

    cmd->end();

    // --- submit ---
    if (m_renderTarget) {
        m_device->submitOffscreen();
    } else {
        m_device->submitAndPresent(m_swapchain.get());
    }
}

// ============================================================
// 离屏回读
// ============================================================

bool EngineView::readbackPixels(std::vector<uint8_t>& pixels) {
    if (!m_renderTarget || !m_stagingBuffer) return false;

    m_device->waitIdle();

    auto cmd = m_device->createCommandList();
    cmd->begin();
    cmd->transitionResource(m_renderTarget->colorTexture(), ResourceState::CopySrc);
    cmd->copyTextureToBuffer(m_renderTarget->colorTexture(), m_stagingBuffer.get());
    cmd->end();

    m_device->executeCommandList(cmd.get());
    m_device->waitIdle();

    uint32_t byteSize = static_cast<uint32_t>(m_width) * m_height * 4;
    pixels.resize(byteSize);
    return m_stagingBuffer->readback(0, byteSize, pixels.data());
}

// ============================================================
// SceneRenderer 初始化
// ============================================================

bool EngineView::initSceneRenderer() {
    TextureFormat colorFmt = m_renderTarget
        ? m_renderTarget->colorFormat()
        : m_swapchain->colorFormat();
    TextureFormat depthFmt = m_renderTarget
        ? m_renderTarget->depthFormat()
        : m_swapchain->depthFormat();
    bool hasDepth = m_renderTarget
        ? m_renderTarget->hasDepth()
        : m_swapchain->hasDepth();

    m_sceneRenderer = std::make_unique<SceneRenderer>(m_device.get());
    if (!m_sceneRenderer->init(colorFmt, depthFmt, hasDepth)) {
        std::fprintf(stderr, "[EngineView] SceneRenderer::init() failed "
                             "(shaders or PSOs not loaded)\n");
        return false;
    }
    return true;
}

// ============================================================
// 输入处理
// ============================================================

void EngineView::handleInput(const InputEvent& event) {
    if (m_operator) {
        m_operator->handleEvent(event, m_camera);
    }
}

// ============================================================
// Operator 管理
// ============================================================

void EngineView::setOperator(std::unique_ptr<Operator> op) {
    if (m_operator) {
        m_operator->onDeactivate(m_camera);
    }
    m_operator = op ? std::move(op) : std::make_unique<CameraManipulator>();
    m_operator->onActivate(m_camera);
}

std::unique_ptr<Operator> EngineView::takeOperator() {
    auto old = std::move(m_operator);
    m_operator = std::make_unique<CameraManipulator>();
    m_operator->onActivate(m_camera);
    return old;
}

void EngineView::setOperatorRaw(Operator* op) {
    if (m_operator) {
        m_operator->onDeactivate(m_camera);
    }
    // release 旧指针（不 delete，因为可能是 setOperatorRaw 设进来的）
    m_operator.release();
    m_operator.reset(op);
    if (m_operator) {
        m_operator->onActivate(m_camera);
    }
}

// ============================================================
// 场景
// ============================================================

void EngineView::setScene(Scene* scene) {
    m_scene = scene;
}

void EngineView::clearScene() {
    m_scene = nullptr;
    m_renderQueue.clear();
}

// ============================================================
// 清理
// ============================================================

void EngineView::cleanup() {
    if (!m_initialized && !m_device) return;
    if (m_device) m_device->waitIdle();

    if (m_scene) {
        m_scene->traverse([](SceneNode& node) {
            if (node.hasRenderData() || node.hasEdgeData()) {
                node.releaseGpuResources();
            }
        });
        m_scene = nullptr;
    }
    m_renderQueue.clear();
    m_sceneRenderer.reset();

    m_stagingBuffer.reset();
    m_renderTarget.reset();
    m_swapchain.reset();
}

void EngineView::shutdown() {
    if (!m_initialized) return;
    if (m_device) m_device->waitIdle();
    cleanup();
    m_initialized = false;
}

} // namespace mulan::Engine
