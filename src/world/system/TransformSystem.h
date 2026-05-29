/**
 * @file TransformSystem.h
 * @brief 世界变换更新 System
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "System.h"
#include "../Entity.h"

#include <vector>

namespace mulan::world {

class TransformSystem : public System {
public:
    TransformSystem() : System(-100) {}

    void update(World& world, float dt) override;

private:
    void propagateChildren(const World& world, Entity::Id root, std::vector<Entity::Id>& out);

    std::vector<Entity::Id> m_dirty;
};

} // namespace mulan::world
