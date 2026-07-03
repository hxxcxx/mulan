/**
 * @file bounds_component.h
 * @brief BoundsComponent —— 用于场景查询和 Fit 的世界空间包围盒缓存
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <mulan/math/math.h>

namespace mulan::scene {

struct BoundsComponent {
    engine::AABB world_bounds;
};

} // namespace mulan::scene
