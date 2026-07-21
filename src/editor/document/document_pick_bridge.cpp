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

std::optional<view::RenderScene::PickResult> DocumentPickBridge::pickAt(const engine::Camera& camera, double x,
                                                                        double y) {
    const view::RenderScene* scene = render_binding_.renderScene();
    if (!scene) {
        return std::nullopt;
    }

    return scene->pick(camera.screenRay(x, y), linePickToleranceWorld(camera));
}

}  // namespace mulan::editor
