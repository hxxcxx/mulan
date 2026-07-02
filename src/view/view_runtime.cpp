#include "view_runtime.h"
#include "mulan/engine/rhi/device.h"
#include "mulan/engine/rhi/render_types.h"
#include "mulan/engine/render/graph/forward_pass.h"
#include "mulan/engine/render/graph/edge_pass.h"
#include "mulan/engine/render/material/material_cache.h"

#include <cstdio>
#include <cstring>

namespace mulan::view {





ViewRuntime::ViewRuntime()
    : default_op_(std::make_unique<engine::CameraManipulator>())
{
    default_op_->setState(engine::Operator::State::Active);
    default_op_->onActivate(camera_);
    camera_.setOrthographic(true);
    camera_.setOrthoSize(5.0);
    camera_.setDistance(10.0);
}

ViewRuntime::~ViewRuntime() {
    shutdown();
}





bool ViewRuntime::init(const ViewConfig& cfg, int width, int height) {
    if (initialized_) return true;

    width_  = width;
    height_ = height;

    
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

    
    gpu_storage_  = std::make_unique<engine::GpuResourceManager>(*device_);
    gpu_      = gpu_storage_.get();

    
    if (!initRendering(width, height)) { cleanup(); return false; }

    
    camera_.setViewport(width, height);
    camera_.fitToBox(engine::AABB(engine::Vec3(-1, -1, -1), engine::Vec3(1, 1, 1)));

    initialized_ = true;
    return true;
}





bool ViewRuntime::initOffscreen(int width, int height) {
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
    ci.enableValidation = true;  

    device_ = engine::RHIDevice::create(ci).value_or(nullptr);
    if (!device_) return false;

    
    engine::RenderTargetDesc rtDesc;
    rtDesc.width       = static_cast<uint32_t>(width);
    rtDesc.height      = static_cast<uint32_t>(height);
    rtDesc.colorFormat = engine::TextureFormat::RGBA8_UNorm;
    rtDesc.depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;
    rtDesc.hasDepth    = true;

    auto rt = device_->createRenderTarget(rtDesc);
    if (!rt) { cleanup(); return false; }
    render_target_ = std::move(*rt);

    
    uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
    auto sb = device_->createBuffer(
        engine::BufferDesc::staging(pixelBytes, "ReadbackStaging"));
    if (!sb) { cleanup(); return false; }
    staging_buffer_ = std::move(*sb);

    
    gpu_storage_  = std::make_unique<engine::GpuResourceManager>(*device_);
    gpu_      = gpu_storage_.get();

    
    if (!initRendering(width, height)) { cleanup(); return false; }

    
    camera_.setViewport(width, height);
    camera_.fitToBox(engine::AABB(engine::Vec3(-1, -1, -1), engine::Vec3(1, 1, 1)));

    initialized_ = true;
    return true;
}





void ViewRuntime::shutdown() {
    if (!initialized_ && !device_) return;
    if (device_) device_->waitIdle();

    view_cube_renderer_.reset();
    gpu_storage_.reset();
    staging_buffer_.reset();
    render_target_.reset();
    swapchain_.reset();
    render_graph_.clear();   

    gpu_       = nullptr;
    device_.reset();

    initialized_    = false;
}





void ViewRuntime::setRenderScene(const render_scene::RenderScene* scene,
                              const asset::AssetLibrary* assets) {
    render_scene_ = scene;
    assets_ = assets;
    renderer_.setScene(scene, assets);
}





bool ViewRuntime::initRendering(int width, int height) {
    width_  = width;
    height_ = height;
    camera_.setViewport(width, height);

    
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

    
    if (!initSceneRenderer()) {
        std::fprintf(stderr, "[ViewRuntime] ViewCube init failed (non-fatal)\n");
    }

    return true;
}

bool ViewRuntime::initSceneRenderer() {
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

void ViewRuntime::cleanup() {
    view_cube_renderer_.reset();
    render_graph_ = engine::RenderGraph();
    staging_buffer_.reset();
    render_target_.reset();
    swapchain_.reset();
    gpu_storage_.reset();
    gpu_       = nullptr;
}





void ViewRuntime::renderFrame() {
    if (!initialized_) return;

    auto* fwd  = render_graph_.pass<engine::ForwardPass>(0);
    auto* edge = render_graph_.pass<engine::EdgePass>(1);

    if (gpu_)
        renderer_.rebuild(*gpu_,
                          fwd ? fwd->pipelineState() : nullptr,
                          edge ? edge->pipelineState() : nullptr);

    if (fwd) {
        fwd->setDrawCommands(renderer_.faceCommands());
    }
    if (edge) {
        edge->setDrawCommands(renderer_.edgeCommands());
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

void ViewRuntime::onFrameEnd() {
}

void ViewRuntime::resize(int width, int height) {
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
                std::fprintf(stderr, "[ViewRuntime] resize staging buffer failed: %s\n",
                             sb.error().message.c_str());
                
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





void ViewRuntime::handleInput(const engine::InputEvent& event) {
    engine::Operator* op = activeOperator();
    if (!op) return;

    op->handleEvent(event, camera_);

    
    
    if (op->isFinished() && !op_stack_.empty()) {
        auto finishedHook = op->finishHook();  
        if (finishedHook) finishedHook(*op);
        popOperator();
    }
}





void ViewRuntime::pushOperator(std::unique_ptr<engine::Operator> op) {
    if (!op) return;

    
    if (auto* cur = activeOperator()) {
        cur->setState(engine::Operator::State::Inactive);
        cur->onDeactivate(camera_);
    }

    op->setState(engine::Operator::State::Active);
    op->onActivate(camera_);
    op_stack_.push_back(std::move(op));
}

void ViewRuntime::popOperator() {
    if (op_stack_.empty()) return;

    
    auto top = std::move(op_stack_.back());
    op_stack_.pop_back();
    top->setState(engine::Operator::State::Inactive);
    top->onDeactivate(camera_);
    top.reset();  

    
    if (auto* next = activeOperator()) {
        next->setState(engine::Operator::State::Active);
        next->onActivate(camera_);
    }
}

engine::Operator* ViewRuntime::activeOperator() const {
    if (!op_stack_.empty()) return op_stack_.back().get();
    return default_op_.get();
}





bool ViewRuntime::readbackPixels(std::vector<uint8_t>& pixels) {
    if (!render_target_ || !staging_buffer_ || !device_) return false;

    device_->waitIdle();

    auto cmdResult = device_->createCommandList();
    if (!cmdResult) {
        std::fprintf(stderr, "[ViewRuntime] readbackPixels createCommandList: %s\n",
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

} 

