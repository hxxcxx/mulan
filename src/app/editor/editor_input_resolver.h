/**
 * @file editor_input_resolver.h
 * @brief Builds tool input from raw viewport input.
 */
#pragma once

#include "editor_input.h"

namespace mulan::app {

class EditorInputResolver {
public:
    const engine::WorkPlane& workPlane() const { return work_plane_; }
    void setWorkPlane(engine::WorkPlane plane) { work_plane_ = plane; }

    EditorInput resolve(const engine::InputEvent& event, const engine::Camera& camera) const;

private:
    engine::WorkPlane work_plane_ = engine::WorkPlane::worldXY();
};

}  // namespace mulan::app
