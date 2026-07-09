#include "document_view_binding.h"

#include "document_session.h"

#include <mulan/io/document.h>
#include <mulan/scene/scene.h>
#include <mulan/view/view_context.h>

#include <algorithm>
#include <cmath>
#include <span>

namespace {

double linePickToleranceWorld(const mulan::engine::Camera& camera) {
    constexpr double kLinePickPixels = 6.0;
    const double viewportHeight = static_cast<double>(std::max(1, camera.height()));
    if (camera.isOrthographic()) {
        return kLinePickPixels * (2.0 * camera.orthoSize()) / viewportHeight;
    }

    const double viewHeightAtTarget =
            2.0 * std::max(camera.distance(), camera.nearPlane()) * std::tan(camera.fieldOfView() * 0.5);
    return kLinePickPixels * viewHeightAtTarget / viewportHeight;
}

}  // namespace

DocumentViewBinding::DocumentViewBinding() = default;

DocumentViewBinding::~DocumentViewBinding() {
    unbind();
}

void DocumentViewBinding::bind(DocumentSession& session, mulan::view::ViewContext& view) {
    unbind();
    session_ = &session;
    view_ = &view;

    syncRenderCache();
    injectRenderCache();
    applyViewPreferences();
}

void DocumentViewBinding::unbind() {
    if (view_) {
        view_->setRenderScene(nullptr, nullptr);
        view_->setSceneLights(std::span<const mulan::engine::Light>{});
    }
    session_ = nullptr;
    view_ = nullptr;
    render_cache_.clear();
}

void DocumentViewBinding::refresh() {
    if (!isBound()) {
        return;
    }
    syncRenderCache();
    fitCameraClipPlanesToSceneBounds();
    injectRenderCache();
    view_->renderFrame();
}

void DocumentViewBinding::fitAll() {
    if (!isBound()) {
        return;
    }
    syncRenderCache();
    const auto& bounds = render_cache_.sceneBounds();
    if (!bounds.isEmpty()) {
        view_->camera().fitToBox(bounds);
    }
    injectRenderCache();
    view_->renderFrame();
}

const mulan::view::RenderScene* DocumentViewBinding::renderScene() const {
    if (!isBound()) {
        return nullptr;
    }
    return render_cache_.renderScene();
}

std::optional<mulan::view::RenderScene::PickResult> DocumentViewBinding::pickAt(const mulan::engine::Camera& camera,
                                                                                double x, double y) {
    if (!isBound()) {
        return std::nullopt;
    }

    syncRenderCache();
    const mulan::view::RenderScene* scene = render_cache_.renderScene();
    if (!scene) {
        return std::nullopt;
    }

    fitCameraClipPlanesToSceneBounds();
    return scene->pick(camera.screenRay(x, y), linePickToleranceWorld(camera));
}

bool DocumentViewBinding::selectSingle(mulan::scene::EntityId entity) {
    if (!isBound() || !session_->document() || !session_->document()->scene()) {
        return false;
    }

    const bool changed = session_->document()->scene()->selectSingle(entity);
    if (changed) {
        refresh();
    }
    return changed;
}

bool DocumentViewBinding::clearSelection() {
    if (!isBound() || !session_->document() || !session_->document()->scene()) {
        return false;
    }

    const bool changed = session_->document()->scene()->clearSelection();
    if (changed) {
        refresh();
    }
    return changed;
}

void DocumentViewBinding::syncRenderCache() {
    if (view_) {
        const bool synced = render_cache_.sync(session_);
        view_->setSceneLights(synced ? render_cache_.lights() : std::span<const mulan::engine::Light>{});
        return;
    }
    render_cache_.sync(session_);
}

void DocumentViewBinding::applyViewPreferences() {
    if (!isBound()) {
        return;
    }

    const auto& preferences = session_->renderPreferences();
    view_->camera().setOrthographic(preferences.preferOrthographic);
    view_->setSurfaceShading(preferences.preferPBRSurface ? mulan::view::SurfaceShading::SurfacePBR
                                                          : mulan::view::SurfaceShading::SolidLit);
    if (preferences.preferIBL) {
        view_->enableIBL();
    }

    const auto& bounds = render_cache_.sceneBounds();
    if (!bounds.isEmpty()) {
        view_->camera().fitToBox(bounds);
    }
}

void DocumentViewBinding::fitCameraClipPlanesToSceneBounds() {
    if (!isBound()) {
        return;
    }

    const auto& bounds = render_cache_.sceneBounds();
    if (!bounds.isEmpty()) {
        view_->camera().fitClipPlanesToBox(bounds);
    }
}

void DocumentViewBinding::injectRenderCache() {
    if (!isBound()) {
        return;
    }
    view_->setRenderScene(render_cache_.renderScene(), render_cache_.assets());
}
