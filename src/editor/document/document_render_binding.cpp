#include "document_render_binding.h"

#include "document_session.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/view/core/view_context.h>

#include <cmath>
#include <utility>

namespace mulan::editor {
namespace {

bool finiteBounds(const math::AABB3& bounds) {
    return !bounds.isEmpty() && std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) &&
           std::isfinite(bounds.min.z) && std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) &&
           std::isfinite(bounds.max.z);
}

void expandFinite(math::AABB3& destination, const math::AABB3& candidate) {
    if (finiteBounds(candidate)) {
        destination.expand(candidate);
    }
}

}  // namespace

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
    prepared_preview_generation_ = view_->previewLayer().generation();
    scene_bounds_dirty_ = false;
    clip_tightening_pending_ = false;
    change_subscription_ =
            session_->subscribeChanges([this](const DocumentChangeStamp& change) { handleDocumentChange(change); });
}

void DocumentRenderBinding::unbind() {
    if (session_ && change_subscription_ != 0) {
        session_->unsubscribeChanges(change_subscription_);
    }
    change_subscription_ = 0;
    if (view_) {
        view_->setRenderScene(nullptr, nullptr);
        view_->setSceneLights(std::span<const engine::Light>{});
    }
    session_ = nullptr;
    view_ = nullptr;
    render_cache_.clear();
    prepared_camera_depth_revision_ = 0;
    prepared_preview_generation_ = 0;
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
    const auto clipSphere = cameraBoundsSphere();
    if (clipSphere.isValid()) {
        view_->camera().fitClipPlanesToSphere(clipSphere);
    }
    prepared_camera_depth_revision_ = view_->camera().depthRevision();
    prepared_preview_generation_ = view_->previewLayer().generation();
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
    const uint64_t previewGeneration = view_->previewLayer().generation();
    const bool mayTighten = mode == ClipUpdateMode::Settled;
    if (!scene_bounds_dirty_ && prepared_camera_depth_revision_ == cameraRevision &&
        prepared_preview_generation_ == previewGeneration && !(mayTighten && clip_tightening_pending_)) {
        return;
    }

    const auto sphere = cameraBoundsSphere();
    if (sphere.isValid()) {
        view_->camera().fitClipPlanesToSphere(
                sphere, 1.2, mayTighten ? engine::ClipPlaneFitMode::Tight : engine::ClipPlaneFitMode::ExpandOnly);
    }
    // 裁剪适配可能在正交模式下后移眼点，必须记录适配后的最终版本。
    prepared_camera_depth_revision_ = view_->camera().depthRevision();
    prepared_preview_generation_ = previewGeneration;
    scene_bounds_dirty_ = false;
    clip_tightening_pending_ = !mayTighten && sphere.isValid();
}

void DocumentRenderBinding::syncRenderCache() {
    MULAN_PROFILE_ZONE();

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

math::Sphere3 DocumentRenderBinding::cameraBoundsSphere() const {
    if (!isBound()) {
        return {};
    }

    math::AABB3 bounds = math::AABB3::empty();
    expandFinite(bounds, render_cache_.sceneBounds());

    const auto& preview = view_->previewLayer();
    for (const view::PreviewDrawable& drawable : preview.drawables()) {
        expandFinite(bounds, drawable.mesh.bounds);
    }

    const view::RenderScene* scene = render_cache_.renderScene();
    const asset::AssetLibrary* assets = render_cache_.assets();
    if (scene && assets) {
        for (const view::PreviewReference& reference : preview.references()) {
            if (!reference.valid()) {
                continue;
            }
            const view::SceneProxy* proxy = scene->proxy(reference.entity);
            if (!proxy || !proxy->visible) {
                continue;
            }
            if (!reference.overrideWorldTransform) {
                expandFinite(bounds, proxy->worldBounds);
                continue;
            }

            const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(assets->asset(proxy->geometry));
            if (geometry) {
                expandFinite(bounds, geometry->localBounds().transformed(reference.worldTransform));
            }
        }
    }

    return math::Sphere3::fromAABB(bounds);
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

void DocumentRenderBinding::handleDocumentChange(const DocumentChangeStamp& change) {
    if (!isBound() || !change.valid()) {
        return;
    }
    if (change.affectsContent()) {
        refresh();
        return;
    }
    if (change.affectsVisualState()) {
        refreshVisualState();
    }
}

}  // namespace mulan::editor
