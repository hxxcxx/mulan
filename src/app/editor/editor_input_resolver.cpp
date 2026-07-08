#include "editor_input_resolver.h"

namespace mulan::app {
namespace {

bool hasCursorPosition(engine::InputEvent::Type type) {
    switch (type) {
    case engine::InputEvent::Type::MousePress:
    case engine::InputEvent::Type::MouseRelease:
    case engine::InputEvent::Type::MouseMove:
    case engine::InputEvent::Type::MouseDoubleClick:
    case engine::InputEvent::Type::Wheel: return true;
    case engine::InputEvent::Type::KeyPress:
    case engine::InputEvent::Type::KeyRelease: return false;
    }
    return false;
}

}  // namespace

EditorInput EditorInputResolver::resolve(const engine::InputEvent& event, const engine::Camera& camera) const {
    EditorInput input;
    input.event = event;
    input.screenX = static_cast<double>(event.x);
    input.screenY = static_cast<double>(event.y);
    input.workPlane = work_plane_;

    if (!hasCursorPosition(event.type) || camera.width() <= 0 || camera.height() <= 0) {
        return input;
    }

    input.cursorRay = camera.screenRay(input.screenX, input.screenY);

    if (auto point = work_plane_.intersectScreen(camera, input.screenX, input.screenY)) {
        input.point = EditorPoint{ *point, EditorPointSource::WorkPlane };
        input.workPoint = *point;
    }

    return input;
}

}  // namespace mulan::app
