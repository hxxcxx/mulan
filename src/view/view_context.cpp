#include "view_context.h"

#include "capture_service.h"

namespace mulan::view {

ViewContext::ViewContext() : default_op_(std::make_unique<engine::CameraManipulator>()) {
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
    if (runtime_host_.isInitialized())
        return true;

    width_ = width;
    height_ = height;
    ibl_enabled_ = cfg.iblEnabled;
    hdr_path_ = cfg.hdrPath;

    if (!runtime_host_.initWindow(cfg, width, height, light_env_)) {
        return false;
    }

    width_ = static_cast<int>(runtime_host_.surfaceWidth());
    height_ = static_cast<int>(runtime_host_.surfaceHeight());

    camera_.setViewport(width_, height_);
    camera_.fitToBox(math::AABB3(math::Point3(-1, -1, -1), math::Point3(1, 1, 1)));

    return true;
}

bool ViewContext::initOffscreen(int width, int height) {
    if (runtime_host_.isInitialized())
        return true;

    width_ = width;
    height_ = height;

    if (!runtime_host_.initOffscreen(width, height, light_env_)) {
        return false;
    }

    width_ = static_cast<int>(runtime_host_.surfaceWidth());
    height_ = static_cast<int>(runtime_host_.surfaceHeight());

    camera_.setViewport(width_, height_);
    camera_.fitToBox(math::AABB3(math::Point3(-1, -1, -1), math::Point3(1, 1, 1)));

    return true;
}

void ViewContext::shutdown() {
    runtime_host_.shutdown();
}

void ViewContext::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    runtime_host_.setRenderScene(scene, assets);
}

void ViewContext::enableIBL() {
    // 两层门控：全局开关 + HDR 路径有效
    if (!ibl_enabled_)
        return;
    runtime_host_.enableIBL(hdr_path_);
}

void ViewContext::renderFrame() {
    if (!runtime_host_.isInitialized())
        return;

    renderFrame(buildViewState());
}

void ViewContext::renderFrame(const ViewState& viewState) {
    if (!runtime_host_.isInitialized())
        return;

    runtime_host_.render(viewState);
    onFrameEnd();
}

ViewState ViewContext::snapshotViewState() const {
    return buildViewState();
}

ViewState ViewContext::snapshotViewState(const engine::Camera& camera, const CaptureVisual& visual, uint32_t width,
                                         uint32_t height) const {
    ViewState state;
    state.viewMatrix = camera.viewMatrix();
    state.projectionMatrix = camera.projectionMatrix();
    state.cameraPosition = camera.eyePosition();
    state.width = static_cast<int>(width);
    state.height = static_cast<int>(height);
    state.showOverlays = visual.showOverlays;
    state.showViewCube = visual.showViewCube;

    switch (visual.style) {
    case CaptureRenderStyle::Shaded:
        state.renderMode = RenderMode::Shaded;
        state.showFaces = true;
        state.showEdges = false;
        break;
    case CaptureRenderStyle::ShadedWithEdges:
        state.renderMode = RenderMode::ShadedWithEdges;
        state.showFaces = true;
        state.showEdges = true;
        break;
    case CaptureRenderStyle::Wireframe:
    case CaptureRenderStyle::EdgesOnly:
        state.renderMode = RenderMode::Wireframe;
        state.showFaces = false;
        state.showEdges = true;
        break;
    }
    return state;
}

void ViewContext::onFrameEnd() {
}

void ViewContext::resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (runtime_host_.isInitialized()) {
        runtime_host_.resize(width, height);
        width_ = static_cast<int>(runtime_host_.surfaceWidth());
        height_ = static_cast<int>(runtime_host_.surfaceHeight());
    }
    camera_.setViewport(width_, height_);
}

void ViewContext::handleInput(const engine::InputEvent& event) {
    engine::Operator* op = activeOperator();
    if (!op)
        return;

    op->handleEvent(event, camera_);

    if (op->isFinished() && !op_stack_.empty()) {
        auto finishedHook = op->finishHook();
        if (finishedHook)
            finishedHook(*op);
        popOperator();
    }
}

void ViewContext::pushOperator(std::unique_ptr<engine::Operator> op) {
    if (!op)
        return;

    if (auto* cur = activeOperator()) {
        cur->setState(engine::Operator::State::Inactive);
        cur->onDeactivate(camera_);
    }

    op->setState(engine::Operator::State::Active);
    op->onActivate(camera_);
    op_stack_.push_back(std::move(op));
}

void ViewContext::popOperator() {
    if (op_stack_.empty())
        return;

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
    if (!op_stack_.empty())
        return op_stack_.back().get();
    return default_op_.get();
}

bool ViewContext::readbackPixels(std::vector<uint8_t>& pixels) {
    return runtime_host_.readbackPixels(pixels);
}

bool ViewContext::isOffscreenSurface() const {
    return runtime_host_.isOffscreenSurface();
}

uint32_t ViewContext::surfaceWidth() const {
    return runtime_host_.surfaceWidth();
}

uint32_t ViewContext::surfaceHeight() const {
    return runtime_host_.surfaceHeight();
}

bool ViewContext::configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height) {
    if (!runtime_host_.configureCaptureSurface(desc, width, height)) {
        return false;
    }
    width_ = static_cast<int>(runtime_host_.surfaceWidth());
    height_ = static_cast<int>(runtime_host_.surfaceHeight());
    camera_.setViewport(width_, height_);
    return true;
}

std::optional<RenderSurfaceDesc> ViewContext::captureSurfaceSnapshot() const {
    return runtime_host_.offscreenSurfaceDesc();
}

bool ViewContext::restoreCaptureSurface(const RenderSurfaceDesc& desc) {
    if (!runtime_host_.configureOffscreenSurface(desc)) {
        return false;
    }
    width_ = static_cast<int>(runtime_host_.surfaceWidth());
    height_ = static_cast<int>(runtime_host_.surfaceHeight());
    camera_.setViewport(width_, height_);
    return true;
}

core::Result<engine::RenderCaptureResult> ViewContext::capture(const engine::RenderCaptureDesc& desc) {
    return CaptureService{}.capture(*this, desc);
}

core::Result<CaptureImage> ViewContext::capture(const CaptureRequest& request) {
    return CaptureService{}.capture(*this, request);
}

CaptureBatchResult ViewContext::capture(const CaptureBatch& batch) {
    return CaptureService{}.capture(*this, batch);
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
    state.showOverlays = show_overlays_;
    state.showViewCube = show_view_cube_;
    return state;
}

}  // namespace mulan::view
