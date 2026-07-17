#include <mulan/view/core/view_context.h>

#include "capture/capture_service.h"

#include "runtime/detail/render_session.h"

#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace mulan::view {
namespace {

math::Quat makeCameraRotation(const math::Vec3& forward, const math::Vec3& preferredUp) {
    const math::Vec3 f = forward.normalized();
    math::Vec3 up = preferredUp.normalized();
    if (std::abs(f.dot(up)) > 0.98) {
        up = math::Vec3(0, 0, 1);
        if (std::abs(f.dot(up)) > 0.98) {
            up = math::Vec3(0, 1, 0);
        }
    }

    const math::Vec3 right = f.cross(up).normalized();
    const math::Vec3 trueUp = right.cross(f).normalized();
    return math::Quat::fromMat3(math::Mat3(right, f, trueUp));
}

}  // namespace

ViewContext::ViewContext()
    : render_session_(std::make_unique<detail::RenderSession>()),
      default_op_(std::make_unique<engine::CameraManipulator>()) {
    default_op_->setState(engine::Operator::State::Active);
    default_op_->onActivate(camera_);
    camera_.setOrthographic(true);
    camera_.setOrthoSize(5.0);
    camera_.setDistance(10.0);
    setCameraToWorldXY();
}

ViewContext::~ViewContext() {
    shutdown();
}

bool ViewContext::init(const ViewConfig& cfg, int width, int height) {
    MULAN_PROFILE_ZONE();

    if (render_session_->isInitialized())
        return true;

    width_ = width;
    height_ = height;
    ibl_enabled_ = cfg.iblEnabled;
    hdr_path_ = cfg.hdrPath;

    if (!render_session_->initWindow(cfg, width, height)) {
        return false;
    }
    render_session_->setPreviewLayer(&preview_layer_);
    render_session_->setLightEnvironment(light_env_);

    const auto surface = render_session_->surfaceState();
    width_ = static_cast<int>(surface.width);
    height_ = static_cast<int>(surface.height);

    camera_.setViewport(width_, height_);
    camera_.fitToBox(math::AABB3(math::Point3(-1, -1, -1), math::Point3(1, 1, 1)));

    return true;
}

bool ViewContext::initOffscreen(const ViewConfig& cfg, int width, int height) {
    if (render_session_->isInitialized())
        return true;

    width_ = width;
    height_ = height;
    ibl_enabled_ = cfg.iblEnabled;
    hdr_path_ = cfg.hdrPath;

    if (!render_session_->initOffscreen(cfg, width, height)) {
        return false;
    }
    render_session_->setPreviewLayer(&preview_layer_);
    render_session_->setLightEnvironment(light_env_);

    const auto surface = render_session_->surfaceState();
    width_ = static_cast<int>(surface.width);
    height_ = static_cast<int>(surface.height);

    camera_.setViewport(width_, height_);
    camera_.fitToBox(math::AABB3(math::Point3(-1, -1, -1), math::Point3(1, 1, 1)));

    return true;
}

bool ViewContext::initOffscreen(int width, int height) {
    return initOffscreen(ViewConfig{}, width, height);
}

void ViewContext::shutdown() {
    render_session_->setPreviewLayer(nullptr);
    render_session_->shutdown();
}

bool ViewContext::isInitialized() const {
    return render_session_->isInitialized();
}

std::optional<Error> ViewContext::runtimeFailure() const {
    return render_session_->runtimeFailure();
}

ResultVoid ViewContext::pollRuntime() {
    return render_session_->pollRuntime();
}

void ViewContext::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    clearHoveredPickId();
    render_session_->setRenderScene(scene, assets);
}

void ViewContext::enableIBL() {
    // 两层门控：全局开关 + HDR 路径有效
    if (!ibl_enabled_)
        return;
    render_session_->enableIBL(hdr_path_);
}

void ViewContext::setHoveredPickId(engine::PickId pickId) {
    hovered_pick_id_ = pickId;
}

void ViewContext::clearHoveredPickId() {
    hovered_pick_id_ = engine::PickId::invalid();
}

void ViewContext::setSelectionVisualState(engine::SelectionVisualState state) {
    selection_visual_state_ = std::move(state);
}

void ViewContext::clearSelectionVisualState() {
    selection_visual_state_.clear();
}

void ViewContext::setSceneLights(std::span<const engine::Light> lights) {
    // 场景同步只替换文档灯列表；查看灯策略、环境光和曝光属于 View 设置。
    light_env_.clearLights();
    for (const auto& light : lights) {
        light_env_.addLight(light);
    }
    render_session_->setLightEnvironment(light_env_);
}

void ViewContext::setLightingMode(engine::LightingMode mode) {
    if (light_env_.mode == mode)
        return;
    light_env_.mode = mode;
    render_session_->setLightEnvironment(light_env_);
}

void ViewContext::setAmbientLight(const math::Vec3& color, double intensity) {
    const auto nonNegativeFinite = [](double value) {
        return std::isfinite(value) ? std::max(0.0, value) : 0.0;
    };
    const math::Vec3 sanitizedColor{
        nonNegativeFinite(color.x),
        nonNegativeFinite(color.y),
        nonNegativeFinite(color.z),
    };
    const double sanitizedIntensity = nonNegativeFinite(intensity);
    if (light_env_.ambientColor == sanitizedColor && light_env_.ambientIntensity == sanitizedIntensity)
        return;
    light_env_.ambientColor = sanitizedColor;
    light_env_.ambientIntensity = sanitizedIntensity;
    render_session_->setLightEnvironment(light_env_);
}

void ViewContext::setExposure(double exposure) {
    const double sanitized = std::isfinite(exposure) ? std::max(0.0, exposure) : 1.0;
    if (light_env_.exposure == sanitized)
        return;
    light_env_.exposure = sanitized;
    render_session_->setLightEnvironment(light_env_);
}

void ViewContext::clearPreview() {
    preview_layer_.clear();
}

void ViewContext::setViewCubeLayout(const engine::ViewCubeLayout& layout) {
    view_cube_model_.setLayout(layout);
}

void ViewContext::setViewCubeSize(uint32_t size) {
    auto layout = view_cube_model_.layout();
    layout.size = size;
    view_cube_model_.setLayout(layout);
}

void ViewContext::setViewCubeMargin(uint32_t margin) {
    auto layout = view_cube_model_.layout();
    layout.margin = margin;
    view_cube_model_.setLayout(layout);
}

void ViewContext::setViewCubeCorner(engine::ViewCubeCorner corner) {
    auto layout = view_cube_model_.layout();
    layout.corner = corner;
    view_cube_model_.setLayout(layout);
}

void ViewContext::resetCamera() {
    engine::Camera camera{ engine::CameraMode::Trackball };
    camera.setViewport(width_, height_);
    camera.setOrthographic(true);
    camera.setOrthoSize(5.0);
    camera.setDistance(10.0);
    camera.setClipPlanes(0.1, 1000.0);
    camera_ = camera;
    setCameraToWorldXY();
}

void ViewContext::setCameraToWorldXY() {
    setCameraToViewCubePart(engine::ViewCubePart{ engine::ViewCubePartType::Face, 0, 0, 1, 0 });
}

void ViewContext::renderFrame() {
    // submitFrame 自身会先 drain worker ACK/失败；这里不能用 isInitialized 前置短路，
    // 否则 worker 刚失败时 owner 永远没有机会消费真实失败事件。
    renderFrame(buildViewState());
}

void ViewContext::renderFrame(const ViewState& viewState) {
    render_session_->submitFrame(viewState);
}

ViewState ViewContext::snapshotViewState(uint32_t width, uint32_t height) const {
    engine::Camera camera = camera_;
    camera.setViewport(static_cast<int>(width), static_cast<int>(height));

    ViewState state = buildViewState();
    state.viewMatrix = camera.viewMatrix();
    state.projectionMatrix = camera.projectionMatrix();
    state.cameraPosition = camera.eyePosition();
    state.width = static_cast<int>(width);
    state.height = static_cast<int>(height);
    return state;
}

ViewState ViewContext::snapshotViewState(const engine::Camera& camera, const CaptureVisual& visual, uint32_t width,
                                         uint32_t height) const {
    ViewState state;
    state.viewMatrix = camera.viewMatrix();
    state.projectionMatrix = camera.projectionMatrix();
    state.cameraPosition = camera.eyePosition();
    state.width = static_cast<int>(width);
    state.height = static_cast<int>(height);
    state.surfaceShading = surface_shading_;
    state.showOverlays = visual.showOverlays;
    state.showViewCube = visual.showViewCube;
    state.viewCubeLayout = view_cube_model_.layout();
    state.viewCubeInteraction = view_cube_interaction_;

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

void ViewContext::resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (render_session_->isInitialized()) {
        const auto surface = render_session_->resize(width, height);
        width_ = static_cast<int>(surface.width);
        height_ = static_cast<int>(surface.height);
    }
    camera_.setViewport(width_, height_);
}

bool ViewContext::handleInput(const engine::InputEvent& event) {
    return dispatchInput(event).handled();
}

bool ViewContext::isCameraNavigating() const {
    const auto* manipulator = dynamic_cast<const engine::CameraManipulator*>(default_op_.get());
    return manipulator && manipulator->isDragging();
}

engine::InputOutcome ViewContext::dispatchInput(const engine::InputEvent& event) {
    // 生命周期取消必须同时终止 ViewCube 的 press/release 事务；它不一定会收到
    // 后续 MouseRelease，不能只清悬停标记而保留 consuming_view_cube_click_。
    if (event.isCancelEvent()) {
        clearViewCubeInteraction();
    }

    if (handleViewCubeInput(event)) {
        return engine::InputOutcome::handledBy(engine::InputDisposition::ViewOverlay);
    }

    engine::Operator* op = activeOperator();
    if (!op) {
        return event.isCancelEvent() ? engine::InputOutcome::handledBy(engine::InputDisposition::Cancelled)
                                     : engine::InputOutcome::ignored();
    }

    const engine::InputOutcome outcome = op->dispatchEvent(event, camera_);

    if (op->isFinished() && !op_stack_.empty()) {
        auto finishedHook = op->finishHook();
        if (finishedHook)
            finishedHook(*op);
        // 完成回调可以调整外部所有权记录；只有该 Operator 仍是栈顶时才弹出，
        // 避免回调移除/替换 Operator 后误弹新的栈顶对象。
        if (activeOperator() == op) {
            popOperator();
        }
    }

    return outcome;
}

bool ViewContext::handleViewCubeInput(const engine::InputEvent& event) {
    if (!show_overlays_ || !show_view_cube_)
        return false;

    if (event.type == engine::InputEvent::Type::MouseMove && event.buttons == engine::MouseButton::None) {
        updateViewCubeHover(event);
        return view_cube_interaction_.hasHoveredPart;
    }

    if (consuming_view_cube_click_) {
        if (event.type == engine::InputEvent::Type::MouseRelease) {
            consuming_view_cube_click_ = false;
            view_cube_interaction_.hasPressedPart = false;
        }
        return event.isMouseEvent();
    }

    if (event.type != engine::InputEvent::Type::MousePress || event.button != engine::MouseButton::Left)
        return false;

    const auto hit = view_cube_model_.pickPart(event.x, event.y, static_cast<uint32_t>(width_),
                                               static_cast<uint32_t>(height_), camera_.viewMatrix());
    if (!hit)
        return false;

    setCameraToViewCubePart(hit.part);
    view_cube_interaction_.pressedPart = hit.part;
    view_cube_interaction_.hasPressedPart = true;
    view_cube_interaction_.hoveredPart = hit.part;
    view_cube_interaction_.hasHoveredPart = true;
    consuming_view_cube_click_ = true;
    return true;
}

void ViewContext::updateViewCubeHover(const engine::InputEvent& event) {
    const auto hit = view_cube_model_.pickPart(event.x, event.y, static_cast<uint32_t>(width_),
                                               static_cast<uint32_t>(height_), camera_.viewMatrix());
    if (hit) {
        view_cube_interaction_.hoveredPart = hit.part;
        view_cube_interaction_.hasHoveredPart = true;
    } else {
        view_cube_interaction_.hasHoveredPart = false;
    }
}

void ViewContext::setCameraToViewCubePart(const engine::ViewCubePart& part) {
    math::Vec3 normal = engine::ViewCubeModel::partNormal(part);
    if (normal.lengthSq() < 1.0e-12) {
        return;
    }

    const math::Vec3 forward = -normal;
    math::Vec3 up(0, 1, 0);
    if (part.x != 0 && part.y == 0 && part.z == 0) {
        up = math::Vec3(0, 0, 1);
    } else if (std::abs(forward.dot(up)) > 0.92) {
        up = math::Vec3(0, 0, 1);
    }

    camera_.setMode(engine::CameraMode::Trackball);
    camera_.setRotation(makeCameraRotation(forward, up));
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

    removeOperator(op_stack_.back().get());
}

bool ViewContext::removeOperator(const engine::Operator* op) {
    if (!op) {
        return false;
    }

    const auto it =
            std::find_if(op_stack_.begin(), op_stack_.end(),
                         [op](const std::unique_ptr<engine::Operator>& candidate) { return candidate.get() == op; });
    if (it == op_stack_.end()) {
        return false;
    }

    const bool wasActive = std::next(it) == op_stack_.end();
    if (wasActive) {
        (*it)->setState(engine::Operator::State::Inactive);
        (*it)->onDeactivate(camera_);
    }

    op_stack_.erase(it);

    if (wasActive) {
        if (auto* next = activeOperator()) {
            next->setState(engine::Operator::State::Active);
            next->onActivate(camera_);
        }
    }
    return true;
}

engine::Operator* ViewContext::activeOperator() const {
    if (!op_stack_.empty())
        return op_stack_.back().get();
    return default_op_.get();
}

uint32_t ViewContext::surfaceWidth() const {
    return width_ > 0 ? static_cast<uint32_t>(width_) : 0;
}

uint32_t ViewContext::surfaceHeight() const {
    return height_ > 0 ? static_cast<uint32_t>(height_) : 0;
}

Result<engine::RenderCaptureResult> ViewContext::capture(const engine::RenderCaptureDesc& desc) {
    return CaptureService{}.capture(*this, desc);
}

Result<CaptureImage> ViewContext::capture(const CaptureRequest& request) {
    return CaptureService{}.capture(*this, request);
}

CaptureBatchResult ViewContext::capture(const CaptureBatch& batch) {
    return CaptureService{}.capture(*this, batch);
}

Result<engine::RenderCaptureResult> ViewContext::captureFrame(const ViewState& viewState,
                                                              const engine::RenderCaptureDesc& desc) {
    return render_session_->capture(viewState, desc);
}

ViewState ViewContext::buildViewState() const {
    ViewState state;
    state.viewMatrix = camera_.viewMatrix();
    state.projectionMatrix = camera_.projectionMatrix();
    state.cameraPosition = camera_.eyePosition();
    state.width = width_;
    state.height = height_;
    state.renderMode = render_mode_;
    state.surfaceShading = surface_shading_;
    state.hoveredPickId = hovered_pick_id_;
    state.selectionVisuals = selection_visual_state_;
    state.showFaces = render_mode_ != RenderMode::Wireframe;
    state.showEdges = render_mode_ != RenderMode::Shaded;
    state.showOverlays = show_overlays_;
    state.showViewCube = show_view_cube_;
    state.viewCubeLayout = view_cube_model_.layout();
    state.viewCubeInteraction = view_cube_interaction_;
    return state;
}

}  // namespace mulan::view
