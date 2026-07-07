/**
 * @file editor_input.h
 * @brief 定义编辑工具使用的结构化输入。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include <mulan/engine/interaction/input_event.h>
#include <mulan/math/math.h>

#include <optional>

namespace mulan::app {

struct EditorInput {
    engine::InputEvent event;
    math::Ray3 cursorRay;
    math::Plane3 workPlane;
    std::optional<math::Point3> workPoint;
};

}  // namespace mulan::app
