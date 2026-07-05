#include "view_context.h"

#include <cstdio>

namespace mulan::view {

ViewContext::ViewContext()
    : default_op_(std::make_unique<engine::CameraManipulator>())
{
    default_op_->setState(engine::Operator::State::Active);
    default_op_->onActivate(camera_);
    camera_.setOrthographic(true);
    camera_.setOrthoSize(5.0);
    camera_.setDistance(10.0);
}

ViewContext::~ViewContext() {
    shutdown();
}

bool ViewContext::init(const ViewConfig& cfg, int width, int height) {
    if (initialized_) return true;

    width_  = width;
    height_ = height;
    ibl_enabled_ = cfg.iblEnabled;
    hdr_path_    = cfg.hdrPath;

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

    if (!surface_.initWindowSurface(*device_, cfg, width, height)) {
        cleanup();
        return false;
    }

    if (!initRendering()) { cleanup(); return false; }

    camera_.setViewport(width, height);
    camera_.fitToBox(math::AABB3(math::Point3(-1, -1, -1), math::Point3(1, 1, 1)));

    initialized_ = true;
    return true;
}

bool ViewContext::initOffscreen(int width, int height) {
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

    if (!surface_.initOffscreenSurface(*device_, width, height)) {
        cleanup();
        return false;
    }

    if (!initRendering()) { cleanup(); return false; }

    camera_.setViewport(width, height);
    camera_.fitToBox(math::AABB3(math::Point3(-1, -1, -1), math::Point3(1, 1, 1)));

    initialized_ = true;
    return true;
}

void ViewContext::shutdown() {
    if (!initialized_ && !device_) return;
    if (device_) {
        renderer_.shutdown(*device_);
        surface_.shutdown(*device_);
    }
    device_.reset();
    initialized_ = false;
}

void ViewContext::setRenderScene(const render_scene::RenderScene* scene,
                                 const asset::AssetLibrary* assets) {
    render_scene_ = scene;
    assets_ = assets;
    renderer_.setScene(scene, assets);
}

bool ViewContext::initRendering() {
    width_  = surface_.width();
    height_ = surface_.height();
    camera_.setViewport(width_, height_);

    return renderer_.init(*device_, light_env_,
                          surface_.colorFormat(*device_),
                          surface_.depthFormat(*device_));
}

void ViewContext::enableIBL() {
    // 两层门控：全局开关 + HDR 路径有效
    if (!ibl_enabled_) return;
    if (!device_ || hdr_path_.empty()) return;
    renderer_.enableIBL(*device_, hdr_path_);
}

void ViewContext::cleanup() {
    if (device_) {
        renderer_.shutdown(*device_);
        surface_.shutdown(*device_);
    }
    device_.reset();
}

void ViewContext::renderFrame() {
    if (!initialized_) return;

    renderer_.render(*device_, surface_, buildViewState());
    onFrameEnd();
}

void ViewContext::onFrameEnd() {
}

void ViewContext::resize(int width, int height) {
    width_  = width;
    height_ = height;
    if (device_ && initialized_) {
        surface_.resize(*device_, width, height);
    }
    camera_.setViewport(width, height);
}

void ViewContext::handleInput(const engine::InputEvent& event) {
    engine::Operator* op = activeOperator();
    if (!op) return;

    op->handleEvent(event, camera_);

    if (op->isFinished() && !op_stack_.empty()) {
        auto finishedHook = op->finishHook();
        if (finishedHook) finishedHook(*op);
        popOperator();
    }
}

void ViewContext::pushOperator(std::unique_ptr<engine::Operator> op) {
    if (!op) return;

    if (auto* cur = activeOperator()) {
        cur->setState(engine::Operator::State::Inactive);
        cur->onDeactivate(camera_);
    }

    op->setState(engine::Operator::State::Active);
    op->onActivate(camera_);
    op_stack_.push_back(std::move(op));
}

void ViewContext::popOperator() {
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

engine::Operator* ViewContext::activeOperator() const {
    if (!op_stack_.empty()) return op_stack_.back().get();
    return default_op_.get();
}

bool ViewContext::readbackPixels(std::vector<uint8_t>& pixels) {
    if (!device_) return false;
    return surface_.readbackPixels(*device_, pixels);
}

ViewState ViewContext::buildViewState() const {
    ViewState state;
    state.viewMatrix = camera_.viewMatrix();
    state.projectionMatrix = camera_.projectionMatrix();
    state.cameraPosition = camera_.eyePosition();
    state.width = width_;
    state.height = height_;
    state.renderMode = render_mode_;
    state.showFaces = render_mode_ != RenderMode::Wireframe;
    state.showEdges = render_mode_ != RenderMode::Shaded;
    state.showViewCube = show_view_cube_;
    return state;
}

} // namespace mulan::view
