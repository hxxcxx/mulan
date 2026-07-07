#include "draw_line_operator.h"

namespace mulan::engine {

DrawLineOperator::DrawLineOperator(PointResolver resolver) : point_resolver_(std::move(resolver)) {
}

void DrawLineOperator::onActivate(Camera& cam) {
    camera_op_.setState(State::Active);
    camera_op_.onActivate(cam);
}

void DrawLineOperator::onDeactivate(Camera& cam) {
    clearPreview();
    camera_op_.onDeactivate(cam);
    camera_op_.setState(State::Inactive);
    first_point_.reset();
}

bool DrawLineOperator::onMousePress(const InputEvent& e, Camera& cam) {
    if (e.button != MouseButton::Left) {
        return false;
    }

    const auto point = resolvePoint(e, cam);
    if (!point) {
        return true;
    }

    if (!first_point_) {
        first_point_ = *point;
        updatePreview(*point);
        return true;
    }

    clearPreview();
    if (commit_callback_) {
        commit_callback_(*first_point_, *point);
    }
    finish(true);
    return true;
}

bool DrawLineOperator::onMouseMove(const InputEvent& e, Camera& cam) {
    if (!first_point_) {
        return false;
    }

    const auto point = resolvePoint(e, cam);
    if (!point) {
        return false;
    }

    updatePreview(*point);
    return true;
}

bool DrawLineOperator::onKeyPress(const InputEvent& e, Camera& cam) {
    (void) cam;
    if (e.key == Key::Escape) {
        cancel();
        return true;
    }
    return false;
}

bool DrawLineOperator::isCameraEvent(const InputEvent& e) const {
    if (e.type == InputEvent::Type::Wheel) {
        return true;
    }
    if (!e.isMouseEvent()) {
        return false;
    }
    return e.button == MouseButton::Middle || e.button == MouseButton::Right ||
           e.isButtonPressed(MouseButton::Middle) || e.isButtonPressed(MouseButton::Right);
}

bool DrawLineOperator::handleCameraEvent(const InputEvent& e, Camera& cam) {
    return camera_op_.handleEvent(e, cam);
}

std::optional<math::Point3> DrawLineOperator::resolvePoint(const InputEvent& e, const Camera& cam) const {
    if (point_resolver_) {
        return point_resolver_(e, cam);
    }
    return work_plane_.intersectScreen(cam, e.x, e.y);
}

void DrawLineOperator::updatePreview(const math::Point3& current) {
    if (preview_callback_ && first_point_) {
        preview_callback_(*first_point_, current);
    }
}

void DrawLineOperator::clearPreview() {
    if (clear_preview_callback_) {
        clear_preview_callback_();
    }
}

void DrawLineOperator::cancel() {
    clearPreview();
    first_point_.reset();
    finish(false);
}

}  // namespace mulan::engine
