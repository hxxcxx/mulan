#include "document_pick_bridge.h"

#include "document_render_binding.h"

#include <algorithm>
#include <cmath>

namespace mulan::editor {
namespace {

double linePickToleranceWorld(const engine::Camera& camera) {
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

void DocumentPickBridge::bind(DocumentRenderBinding& renderBinding) {
    render_binding_ = &renderBinding;
}

void DocumentPickBridge::unbind() {
    render_binding_ = nullptr;
}

std::optional<view::RenderScene::PickResult> DocumentPickBridge::pickAt(const engine::Camera& camera, double x,
                                                                        double y) {
    if (!render_binding_ || !render_binding_->isBound()) {
        return std::nullopt;
    }

    const view::RenderScene* scene = render_binding_->renderScene();
    if (!scene) {
        return std::nullopt;
    }

    return scene->pick(camera.screenRay(x, y), linePickToleranceWorld(camera));
}

}  // namespace mulan::editor
