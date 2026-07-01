/**
 * @file BoundsSystem.h
 * @brief 包围盒更新 System
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "system.h"

namespace mulan::world {

class BoundsSystem : public System {
public:
    BoundsSystem() : System(-50) {}

    void update(World& world, float dt) override;
};

} // namespace mulan::world
