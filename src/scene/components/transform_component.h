/**
 * @file transform_component.h
 * @brief TransformComponent —— 场景实体的局部与世界变换
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <mulan/math/math.h>

namespace mulan::scene {

struct TransformComponent {
    math::Mat4 local{ 1.0 };
    math::Mat4 world{ 1.0 };
};

}  // namespace mulan::scene
