#include "viewport.h"
#include "mulan/engine/rhi/device.h"
#include "mulan/engine/rhi/render_types.h"
#include "mulan/engine/render/graph/forward_pass.h"
#include "mulan/engine/render/graph/edge_pass.h"
#include "mulan/engine/render/material/material_cache.h"
#include "mulan/asset/asset_library.h"
#include "mulan/asset/brep_asset.h"
#include "mulan/asset/mesh_asset.h"
#include "mulan/render_scene/render_scene.h"
#include "mulan/render_scene/scene_proxy.h"

#include <cstdio>
#include <cstring>

namespace mulan::view {

// ============================================================
// 旧构造（向后兼容）
// ============================================================

Viewport::Viewport()
    : default_op_(std::make_unique<engine::CameraManipulator>())
{
    default_op_->setState(engine::Operator::State::Active);
    default_op_->onActivate(camera_);
    camera_.setOrthographic(true);
    camera_.setOrthoSize(5.0);
    camera_.setDistance(10.0);
}

Viewport::~Viewport() {
    shutdown();
}

// ============================================================
// init（窗口模式）
// ============================================================

bool Viewport::init(const ViewConfig& cfg, int width, int height) {
    if (initialized_) return true;

    width_  = width;
    height_ = height;

    // --- 从 ViewConfig 构造 DeviceCreateInfo ---
    engine::NativeWindowHandle window = cfg.toNativeWindowHandle();
    if (!window.valid()) return false;

    engine::RenderConfig renderCfg = cfg.toRenderConfig();

    engine::DeviceCreateInfo ci;
    ci.backend          = cfg.backend;
    ci.window           = window;
    ci.renderConfig     = renderCfg;
    ci.enableValidation = cfg.enableValidation;

    device_ = engine::RHIDevice::create(ci).value_or(nullptr);
    if (!device_) return false;

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

    auto sc = device_->createSwapChain(scDesc);
    if (!sc) { cleanup(); return false; }
    swapchain_ = std::move(*sc);

    // --- GPU ---
    gpu_storage_  = std::make_unique<engine::GpuResourceManager>(*device_);
    gpu_      = gpu_storage_.get();

    // --- RenderGraph ---
    if (!initRendering(width, height)) { cleanup(); return false; }

    // --- Camera ---
    camera_.setViewport(width, height);
    camera_.fitToBox(engine::AABB(engine::Vec3(-1, -1, -1), engine::Vec3(1, 1, 1)));

    initialized_ = true;
    return true;
}

// ============================================================
// initOffscreen（离屏模式）
// ============================================================

bool Viewport::initOffscreen(int width, int height) {
    if (initialized_) return true;

    width_  = width;
    height_ = height;

    engine::RenderConfig config;
    config.bufferCount   = 2;
    config.vsync         = false;
    config.depthBuffer   = true;
    config.stencilBuffer = false;

    engine::DeviceCreateInfo ci;
    ci.backend          = engine::GraphicsBackend::Vulkan;
    ci.window           = {};
    ci.renderConfig     = config;
    ci.enableValidation = true;  // 开启 debug layer + InfoQueue 以定位 device removed

    device_ = engine::RHIDevice::create(ci).value_or(nullptr);
    if (!device_) return false;

    // --- RenderTarget ---
    engine::RenderTargetDesc rtDesc;
    rtDesc.width       = static_cast<uint32_t>(width);
    rtDesc.height      = static_cast<uint32_t>(height);
    rtDesc.colorFormat = engine::TextureFormat::RGBA8_UNorm;
    rtDesc.depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;
    rtDesc.hasDepth    = true;

    auto rt = device_->createRenderTarget(rtDesc);
    if (!rt) { cleanup(); return false; }
    render_target_ = std::move(*rt);

    // --- Staging buffer ---
    uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
    auto sb = device_->createBuffer(
        engine::BufferDesc::staging(pixelBytes, "ReadbackStaging"));
    if (!sb) { cleanup(); return false; }
    staging_buffer_ = std::move(*sb);

    // --- GPU ---
    gpu_storage_  = std::make_unique<engine::GpuResourceManager>(*device_);
    gpu_      = gpu_storage_.get();

    // --- RenderGraph ---
    if (!initRendering(width, height)) { cleanup(); return false; }

    // --- Camera ---
    camera_.setViewport(width, height);
    camera_.fitToBox(engine::AABB(engine::Vec3(-1, -1, -1), engine::Vec3(1, 1, 1)));

    initialized_ = true;
    return true;
}

// ============================================================
// shutdown
// ============================================================

void Viewport::shutdown() {
    if (!initialized_ && !device_) return;
    if (device_) device_->waitIdle();

    view_cube_renderer_.reset();
    gpu_storage_.reset();
    staging_buffer_.reset();
    render_target_.reset();
    swapchain_.reset();
    render_graph_.clear();   // Pass 持有 UBO 等 GPU 资源，必须在 device 析构前释放

    gpu_       = nullptr;
    device_.reset();

    initialized_    = false;
}

// ============================================================
// World 绑定
// ============================================================

void Viewport::setRenderScene(const render_scene::RenderScene* scene,
                              const asset::AssetLibrary* assets) {
    render_scene_ = scene;
    assets_ = assets;
}

// ============================================================
// initRendering（旧路径 + 新路径共用）
// ============================================================

bool Viewport::initRendering(int width, int height) {
    width_  = width;
    height_ = height;
    camera_.setViewport(width, height);

    // 用实际渲染目标的格式创建 Pass，避免 PSO 的 RenderPass 与 SwapChain/RT 不兼容
    engine::TextureFormat colorFmt = render_target_
        ? render_target_->colorFormat()
        : (swapchain_ ? swapchain_->colorFormat() : engine::TextureFormat::RGBA8_UNorm);
    engine::TextureFormat depthFmt = render_target_
        ? render_target_->depthFormat()
        : (swapchain_ ? swapchain_->depthFormat() : engine::TextureFormat::D32_Float);

    auto& matCache = engine::MaterialCache::instance();
    matCache.setDevice(device_.get());

    auto fwd = std::make_unique<engine::ForwardPass>(
        *device_, *gpu_, matCache, camera_, light_env_);
    if (!fwd->init(colorFmt, depthFmt, true))
        return false;
    render_graph_.addPass(std::move(fwd));

    auto edge = std::make_unique<engine::EdgePass>(
        *device_, *gpu_, matCache, camera_, light_env_);
    if (!edge->init(colorFmt, depthFmt, true))
        return false;
    render_graph_.addPass(std::move(edge));

    // ViewCube
    if (!initSceneRenderer()) {
        std::fprintf(stderr, "[Viewport] ViewCube init failed (non-fatal)\n");
    }

    return true;
}

bool Viewport::initSceneRenderer() {
    engine::TextureFormat colorFmt = render_target_
        ? render_target_->colorFormat()
        : (swapchain_ ? swapchain_->colorFormat() : engine::TextureFormat::RGBA8_UNorm);
    engine::TextureFormat depthFmt = render_target_
        ? render_target_->depthFormat()
        : (swapchain_ ? swapchain_->depthFormat() : engine::TextureFormat::D32_Float);

    view_cube_renderer_ = std::make_unique<engine::ViewCubeRenderer>(device_.get());
    if (!view_cube_renderer_->init(colorFmt, depthFmt)) {
        view_cube_renderer_.reset();
        return false;
    }
    return true;
}

void Viewport::cleanup() {
    view_cube_renderer_.reset();
    render_graph_ = engine::RenderGraph();
    staging_buffer_.reset();
    render_target_.reset();
    swapchain_.reset();
    gpu_storage_.reset();
    gpu_       = nullptr;
}

// ============================================================
// renderFrame（新路径：完整帧循环）
// ============================================================

void Viewport::renderFrame() {
    if (!initialized_) return;

    auto* fwd  = render_graph_.pass<engine::ForwardPass>(0);
    auto* edge = render_graph_.pass<engine::EdgePass>(1);

    rebuildDrawCommands();

    if (fwd) {
        fwd->setDrawCommands(face_cmds_);
    }
    if (edge) {
        edge->setDrawCommands(edge_cmds_);
    }

    device_->beginFrame(swapchain_ ? swapchain_.get() : nullptr);
    auto* cmd = device_->frameCommandList();
    cmd->begin();

    if (render_target_)
        cmd->beginRenderPass(render_target_->renderPassBeginInfo());
    else if (swapchain_)
        cmd->beginRenderPass(swapchain_->renderPassBeginInfo());

    engine::Viewport vp;
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(width_);
    vp.height   = static_cast<float>(height_);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->setViewport(vp);

    engine::ScissorRect sc;
    sc.x      = 0;
    sc.y      = 0;
    sc.width  = static_cast<int32_t>(width_);
    sc.height = static_cast<int32_t>(height_);
    cmd->setScissorRect(sc);

    engine::PassContext ctx;
    ctx.cmd    = cmd;
    ctx.width  = width_;
    ctx.height = height_;
    render_graph_.execute(ctx);

    // ViewCube（叠加在右下角）
    if (view_cube_renderer_) {
        view_cube_renderer_->render(cmd, camera_,
                                   static_cast<uint32_t>(width_),
                                   static_cast<uint32_t>(height_));
    }

    cmd->endRenderPass();
    cmd->end();

    if (render_target_)
        device_->submitOffscreen();
    else
        device_->submitAndPresent(swapchain_.get());

    onFrameEnd();
}

void Viewport::rebuildDrawCommands() {
    face_cmds_.clear();
    edge_cmds_.clear();

    if (!render_scene_ || !assets_ || !gpu_)
        return;

    auto* fwd  = render_graph_.pass<engine::ForwardPass>(0);
    auto* edge = render_graph_.pass<engine::EdgePass>(1);
    auto& matCache = engine::MaterialCache::instance();

    uint32_t nextObjectOffset = 0;
    render_scene_->forEachProxy([&](const render_scene::SceneProxy& proxy) {
        if (!proxy.visible || !proxy.geometry)
            return;

        const auto* asset = assets_->asset(proxy.geometry);
        const engine::Mesh* faceMesh = nullptr;
        const engine::Mesh* edgeMesh = nullptr;

        if (const auto* brep = dynamic_cast<const asset::BRepAsset*>(asset)) {
            faceMesh = &brep->faceMesh();
            edgeMesh = &brep->edgeMesh();
        } else if (const auto* mesh = dynamic_cast<const asset::MeshAsset*>(asset)) {
            faceMesh = &mesh->mesh();
        }

        const uint64_t key = proxy.geometry.value;
        if (faceMesh && !faceMesh->empty()) {
            if (!gpu_->faceGeometry(key))
                gpu_->uploadFaceMesh(key, *faceMesh);

            if (const auto* geo = gpu_->faceGeometry(key)) {
                engine::MeshDrawCommand cmd;
                cmd.pipelineState = fwd ? fwd->pipelineState() : nullptr;
                cmd.vertexBuffer = geo->vertexBuffer.get();
                cmd.indexBuffer = geo->indexBuffer.get();
                cmd.indexCount = geo->indexCount;
                cmd.instanceCount = 1;
                cmd.topology = faceMesh->topology;
                cmd.objectUboOffset = nextObjectOffset;
                cmd.materialUboOffset = matCache.materialGpuOffset(0);
                cmd.worldTransform = proxy.worldTransform;
                cmd.pickId = proxy.entity.index();
                cmd.selected = proxy.selected;
                face_cmds_.push_back(std::move(cmd));
                nextObjectOffset += engine::MeshDrawCommand::kObjectUboStride;
            }
        }

        if (edgeMesh && !edgeMesh->empty()) {
            if (!gpu_->edgeGeometry(key))
                gpu_->uploadEdgeMesh(key, *edgeMesh);

            if (const auto* geo = gpu_->edgeGeometry(key)) {
                engine::MeshDrawCommand cmd;
                cmd.pipelineState = edge ? edge->pipelineState() : nullptr;
                cmd.vertexBuffer = geo->vertexBuffer.get();
                cmd.indexBuffer = geo->indexBuffer.get();
                cmd.indexCount = geo->indexCount;
                cmd.instanceCount = 1;
                cmd.topology = edgeMesh->topology;
                cmd.objectUboOffset = nextObjectOffset;
                cmd.worldTransform = proxy.worldTransform;
                cmd.pickId = proxy.entity.index();
                cmd.selected = proxy.selected;
                cmd.isEdge = true;
                edge_cmds_.push_back(std::move(cmd));
                nextObjectOffset += engine::MeshDrawCommand::kObjectUboStride;
            }
        }
    });
}

void Viewport::onFrameEnd() {
}

void Viewport::resize(int width, int height) {
    width_  = width;
    height_ = height;

    if (device_ && initialized_) {
        device_->waitIdle();

        if (render_target_) {
            render_target_->resize(width, height);

            staging_buffer_.reset();
            uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
            auto sb = device_->createBuffer(
                engine::BufferDesc::staging(pixelBytes, "ReadbackStaging"));
            if (!sb) {
                std::fprintf(stderr, "[Viewport] resize staging buffer failed: %s\n",
                             sb.error().message.c_str());
                // staging_buffer_ 保持空，readbackPixels 前置判空会挡住
            } else {
                staging_buffer_ = std::move(*sb);
            }
        } else if (swapchain_) {
            device_->clearCaches();
            swapchain_->resize(width, height);
        }
    }
    camera_.setViewport(width, height);
}

// ============================================================
// 输入
// ============================================================

void Viewport::handleInput(const engine::InputEvent& event) {
    engine::Operator* op = activeOperator();
    if (!op) return;

    op->handleEvent(event, camera_);

    // 状态机检测：模态 Operator 调用 finish() 后，由 Viewport 在外部触发
    // 回调并 pop（避免在 Operator 成员函数内析构自身的 UB）。
    if (op->isFinished() && !op_stack_.empty()) {
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
        cur->onDeactivate(camera_);
    }

    op->setState(engine::Operator::State::Active);
    op->onActivate(camera_);
    op_stack_.push_back(std::move(op));
}

void Viewport::popOperator() {
    if (op_stack_.empty()) return;

    // 析构栈顶（先 deactivate）
    auto top = std::move(op_stack_.back());
    op_stack_.pop_back();
    top->setState(engine::Operator::State::Inactive);
    top->onDeactivate(camera_);
    top.reset();  // 析构

    // 恢复下层
    if (auto* next = activeOperator()) {
        next->setState(engine::Operator::State::Active);
        next->onActivate(camera_);
    }
}

engine::Operator* Viewport::activeOperator() const {
    if (!op_stack_.empty()) return op_stack_.back().get();
    return default_op_.get();
}

// ============================================================
// 离屏回读
// ============================================================

bool Viewport::readbackPixels(std::vector<uint8_t>& pixels) {
    if (!render_target_ || !staging_buffer_ || !device_) return false;

    device_->waitIdle();

    auto cmdResult = device_->createCommandList();
    if (!cmdResult) {
        std::fprintf(stderr, "[Viewport] readbackPixels createCommandList: %s\n",
                     cmdResult.error().message.c_str());
        return false;
    }
    auto cmd = std::move(*cmdResult);
    cmd->begin();
    cmd->transitionResource(render_target_->colorTexture(), engine::ResourceState::CopySrc);
    cmd->copyTextureToBuffer(render_target_->colorTexture(), staging_buffer_.get());
    cmd->end();

    device_->executeCommandList(cmd.get());
    device_->waitIdle();

    uint32_t byteSize = static_cast<uint32_t>(width_) * height_ * 4;
    pixels.resize(byteSize);
    return staging_buffer_->readback(0, byteSize, pixels.data());
}

} // namespace mulan::view
