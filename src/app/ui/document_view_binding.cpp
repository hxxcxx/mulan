#include "document_view_binding.h"

#include "document_session.h"

#include <mulan/io/document.h>
#include <mulan/scene/scene.h>
#include <mulan/view/render_scene.h>
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

struct DocumentViewBinding::RenderCache {
    mulan::view::RenderScene renderScene;
};

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
    render_cache_.reset();
}

void DocumentViewBinding::refresh() {
    if (!isBound()) {
        return;
    }
    syncRenderCache();
    injectRenderCache();
    view_->renderFrame();
}

void DocumentViewBinding::fitAll() {
    if (!isBound()) {
        return;
    }
    const auto& bounds = render_cache_->renderScene.sceneBounds();
    if (!bounds.isEmpty()) {
        view_->camera().fitToBox(bounds);
    }
    view_->renderFrame();
}

const mulan::view::RenderScene* DocumentViewBinding::renderScene() const {
    if (!isBound() || !render_cache_) {
        return nullptr;
    }
    return &render_cache_->renderScene;
}

std::optional<mulan::view::RenderScene::PickResult> DocumentViewBinding::pickAt(const mulan::engine::Camera& camera,
                                                                                double x, double y) {
    if (!isBound()) {
        return std::nullopt;
    }

    syncRenderCache();
    if (!render_cache_) {
        return std::nullopt;
    }

    return render_cache_->renderScene.pick(camera.screenRay(x, y), linePickToleranceWorld(camera));
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
    if (!render_cache_) {
        render_cache_ = std::make_unique<RenderCache>();
    }

    if (!session_ || !session_->document() || !session_->document()->scene() || !session_->document()->assets()) {
        render_cache_->renderScene.clear();
        if (view_) {
            view_->setSceneLights(std::span<const mulan::engine::Light>{});
        }
        return;
    }

    render_cache_->renderScene.sync(*session_->document()->scene(), *session_->document()->assets());
    if (view_) {
        view_->setSceneLights(render_cache_->renderScene.lights());
    }
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

    const auto& bounds = render_cache_->renderScene.sceneBounds();
    if (!bounds.isEmpty()) {
        view_->camera().fitToBox(bounds);
    }
}

void DocumentViewBinding::injectRenderCache() {
    if (!isBound()) {
        return;
    }
    auto* document = session_->document();
    view_->setRenderScene(render_cache_ ? &render_cache_->renderScene : nullptr,
                          document ? document->assets() : nullptr);
}
