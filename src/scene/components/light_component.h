/**
 * @file light_component.h
 * @brief 场景光源组件
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include <mulan/math/math.h>

namespace mulan::scene {

enum class LightKind {
    Directional,
    Point,
    Spot,
};

struct LightComponent {
    LightKind kind = LightKind::Directional;
    math::Vec3 color{ 1.0, 1.0, 1.0 };
    double intensity = 1.0;
    double range = 0.0;
    double innerConeAngle = 0.0;
    double outerConeAngle = 0.7853981633974483;
};

}  // namespace mulan::scene
