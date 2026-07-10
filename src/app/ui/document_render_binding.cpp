#include "document_render_binding.h"

#include "document_session.h"

#include <mulan/view/view_context.h>

#include <algorithm>
#include <cmath>

namespace mulan::app {
namespace {

bool sphereChanged(const math::Sphere3& before, const math::Sphere3& after) {
    if (before.isValid() != after.isValid()) {
        return true;
    }
    if (!after.isValid()) {
        return false;
    }
    const double scale = std::max({ 1.0, before.radius, after.radius });
    return before.center.distance(after.center) > scale * 1.0e-9 ||
           std::abs(before.radius - after.radius) > scale * 1.0e-9;
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
}

void DocumentRenderBinding::unbind() {
    if (view_) {
        view_->setRenderScene(nullptr, nullptr);
        view_->setSceneLights(std::span<const engine::Light>{});
    }
    session_ = nullptr;
    view_ = nullptr;
    render_cache_.clear();
}

void DocumentRenderBinding::refresh() {
    if (!isBound()) {
        return;
    }
    const math::Sphere3 previousSphere = render_cache_.sceneBoundsSphere();
    syncRenderCache();
    const math::Sphere3& currentSphere = render_cache_.sceneBoundsSphere();
    if (sphereChanged(previousSphere, currentSphere)) {
        // 仅实体/可见性/变换改变世界范围时自动扩展视口；选择变化不会触发。
        view_->camera().fitToSphere(currentSphere);
    } else {
        fitCameraClipPlanesToSceneBounds();
    }
    injectRenderCache();
    view_->renderFrame();
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
    injectRenderCache();
    view_->renderFrame();
}

void DocumentRenderBinding::updateCameraClipPlanes() {
    fitCameraClipPlanesToSceneBounds();
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

void DocumentRenderBinding::fitCameraClipPlanesToSceneBounds() {
    if (!isBound()) {
        return;
    }

    const auto& sphere = render_cache_.sceneBoundsSphere();
    if (sphere.isValid()) {
        view_->camera().fitClipPlanesToSphere(sphere);
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
    view_->camera().setOrthographic(preferences.preferOrthographic);
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

}  // namespace mulan::app
