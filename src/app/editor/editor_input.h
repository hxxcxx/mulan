/**
 * @file editor_input.h
 * @brief 定义编辑工具使用的结构化输入。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include <mulan/engine/interaction/input_event.h>
#include <mulan/engine/interaction/work_plane.h>
#include <mulan/math/math.h>

#include <optional>

namespace mulan::app {

enum class EditorPointSource {
    None,
    WorkPlane,
    Snap,
    Pick,
};

struct EditorPoint {
    math::Point3 world;
    EditorPointSource source = EditorPointSource::None;
};

struct EditorInput {
    engine::InputEvent event;
    math::Ray3 cursorRay;
    engine::WorkPlane workPlane = engine::WorkPlane::worldXY();
    double screenX = 0.0;
    double screenY = 0.0;
    std::optional<EditorPoint> point;
    std::optional<math::Point3> workPoint;

    std::optional<math::Point3> worldPoint() const {
        if (point) {
            return point->world;
        }
        return workPoint;
    }
};

}  // namespace mulan::app
