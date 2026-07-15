#include "document_render_binding.h"

#include "document_session.h"

#include <mulan/view/core/view_context.h>

#include <utility>

namespace mulan::editor {

DocumentRenderBinding::~DocumentRenderBinding() {
    unbind();
}

void DocumentRenderBinding::bind(DocumentSession& session, view::ViewContext& view) {
    unbind();
    session_ = &session;
    view_ = &view;

    syncRenderCache();
    injectRenderCache();
    applyViewPreferences();
    prepared_camera_depth_revision_ = view_->camera().depthRevision();
    scene_bounds_dirty_ = false;
    clip_tightening_pending_ = false;
}

void DocumentRenderBinding::unbind() {
    if (view_) {
        view_->setRenderScene(nullptr, nullptr);
        view_->setSceneLights(std::span<const engine::Light>{});
    }
    session_ = nullptr;
    view_ = nullptr;
    render_cache_.clear();
    prepared_camera_depth_revision_ = 0;
    scene_bounds_dirty_ = false;
    clip_tightening_pending_ = false;
}

void DocumentRenderBinding::setFrameInvalidationCallback(FrameInvalidationCallback callback) {
    frame_invalidation_callback_ = std::move(callback);
}

void DocumentRenderBinding::refresh() {
    if (!isBound()) {
        return;
    }
    syncRenderCache();
    // 场景变化只标记深度范围失效；裁剪面在下一帧快照前统一求值。
    scene_bounds_dirty_ = true;
    injectRenderCache();
    invalidateFrame();
}

void DocumentRenderBinding::refreshVisualState() {
    if (!isBound()) {
        return;
    }
    syncRenderCache();
    injectRenderCache();
    invalidateFrame();
}

void DocumentRenderBinding::fitAll() {
    if (!isBound()) {
        return;
    }
    syncRenderCache();
    const auto& sphere = render_cache_.sceneBoundsSphere();
    if (sphere.isValid()) {
        view_->camera().fitToSphere(sphere);
    }
    prepared_camera_depth_revision_ = view_->camera().depthRevision();
    scene_bounds_dirty_ = false;
    clip_tightening_pending_ = false;
    injectRenderCache();
    invalidateFrame();
}

void DocumentRenderBinding::prepareFrame(ClipUpdateMode mode) {
    if (!isBound()) {
        return;
    }

    const uint64_t cameraRevision = view_->camera().depthRevision();
    const bool mayTighten = mode == ClipUpdateMode::Settled;
    if (!scene_bounds_dirty_ && prepared_camera_depth_revision_ == cameraRevision &&
        !(mayTighten && clip_tightening_pending_)) {
        return;
    }

    fitCameraClipPlanesToSceneBounds(mayTighten ? engine::ClipPlaneFitMode::Tight
                                                : engine::ClipPlaneFitMode::ExpandOnly);
    prepared_camera_depth_revision_ = cameraRevision;
    scene_bounds_dirty_ = false;
    clip_tightening_pending_ = !mayTighten && render_cache_.sceneBoundsSphere().isValid();
}

void DocumentRenderBinding::syncRenderCache() {
    if (view_) {
        const bool synced = render_cache_.sync(session_);
        view_->setSceneLights(synced ? render_cache_.lights() : std::span<const engine::Light>{});
        return;
    }
    render_cache_.sync(session_);
}

void DocumentRenderBinding::injectRenderCache() {
    if (!isBound()) {
        return;
    }
    view_->setRenderScene(render_cache_.renderScene(), render_cache_.assets());
}

void DocumentRenderBinding::fitCameraClipPlanesToSceneBounds(engine::ClipPlaneFitMode mode) {
    if (!isBound()) {
        return;
    }

    const auto& sphere = render_cache_.sceneBoundsSphere();
    if (sphere.isValid()) {
        view_->camera().fitClipPlanesToSphere(sphere, 1.2, mode);
    }
}

const view::RenderScene* DocumentRenderBinding::renderScene() const {
    if (!isBound()) {
        return nullptr;
    }
    return render_cache_.renderScene();
}

void DocumentRenderBinding::applyViewPreferences() {
    if (!isBound()) {
        return;
    }

    const auto& preferences = session_->renderPreferences();
    // 文档是视图相机状态的生命周期边界。空文档也必须从确定状态开始，
    // 不能继承上一个文档的 pan、zoom、裁剪面或观察方向。
    view_->resetCamera();
    view_->camera().setOrthographic(preferences.preferOrthographic);
    // 每个新建、打开或切换到的文档都从世界 XY 正视图开始；后续 fit 只调整中心和距离。
    view_->setCameraToWorldXY();
    view_->setSurfaceShading(preferences.preferPBRSurface ? view::SurfaceShading::SurfacePBR
                                                          : view::SurfaceShading::SolidLit);
    if (preferences.preferIBL) {
        view_->enableIBL();
    }

    const auto& sphere = render_cache_.sceneBoundsSphere();
    if (sphere.isValid()) {
        view_->camera().fitToSphere(sphere);
    }
}

void DocumentRenderBinding::invalidateFrame() const {
    if (frame_invalidation_callback_) {
        frame_invalidation_callback_();
    }
}

}  // namespace mulan::editor
