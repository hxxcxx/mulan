#include "capture_request.h"

namespace mulan::view {

ViewState makeCaptureViewState(const engine::Camera& camera,
                               const CaptureVisual& visual,
                               uint32_t width,
                               uint32_t height) {
    ViewState state;
    state.viewMatrix = camera.viewMatrix();
    state.projectionMatrix = camera.projectionMatrix();
    state.cameraPosition = camera.eyePosition();
    state.width = static_cast<int>(width);
    state.height = static_cast<int>(height);
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

} // namespace mulan::view
